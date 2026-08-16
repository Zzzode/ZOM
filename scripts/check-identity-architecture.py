#!/usr/bin/env python3

from __future__ import annotations

import argparse
import copy
import functools
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCHEMA = Path("products/zomlang/compiler/ast/schema.yml")
MANIFEST = Path("products/zomlang/compiler/identity/definition-producers.json")
DEFINITION_KEY = Path("products/zomlang/compiler/identity/key/definition-key.h")
DEFINITION_KEY_IMPLEMENTATION = Path("products/zomlang/compiler/identity/key/definition-key.cc")
HANDLE = Path("products/zomlang/compiler/identity/handle.h")
FROZEN_REGISTRY = Path("products/zomlang/compiler/identity/frozen-registry.h")
SEMANTIC_IDENTITY_REGISTRY = Path(
    "products/zomlang/compiler/identity/semantic-identity-registry-set.h"
)
CANONICAL_IDENTITY_INTERNER = Path(
    "products/zomlang/compiler/identity/canonical/identity-interner-set.h"
)
CANONICAL_IDENTITY_INTERNER_IMPLEMENTATION = Path(
    "products/zomlang/compiler/identity/canonical/identity-interner-set.cc"
)
IDENTITY_INVARIANT = Path("products/zomlang/compiler/identity/identity-invariant.h")
IDENTITY_DUMP_IMPLEMENTATION = Path("products/zomlang/compiler/identity/identity-dump.cc")
SEMANTIC_CONTEXT_FINGERPRINT = Path(
    "products/zomlang/compiler/identity/semantic/context-fingerprint.h"
)
BUILD_SCRIPT_KEY = Path("products/zomlang/compiler/identity/key/build-script-key.h")
BUILD_SCRIPT_KEY_IMPLEMENTATION = Path("products/zomlang/compiler/identity/key/build-script-key.cc")
CRATE_KEY = Path("products/zomlang/compiler/identity/key/crate-key.h")
COMPILATION_UNIT_KEY = Path("products/zomlang/compiler/identity/key/compilation-unit-key.h")
COMPILATION_UNIT_KEY_IMPLEMENTATION = Path(
    "products/zomlang/compiler/identity/key/compilation-unit-key.cc"
)
SOURCE_KEY = Path("products/zomlang/compiler/identity/key/source-key.h")
SOURCE_KEY_IMPLEMENTATION = Path("products/zomlang/compiler/identity/key/source-key.cc")
MODULE_RESOLUTION_KEY = Path("products/zomlang/compiler/identity/key/module-resolution-key.h")
MODULE_RESOLUTION_KEY_IMPLEMENTATION = Path(
    "products/zomlang/compiler/identity/key/module-resolution-key.cc"
)
SEMANTIC_IMPORT_BINDING_KEY = Path(
    "products/zomlang/compiler/identity/key/import-binding-key.h"
)
SEMANTIC_IMPORT_BINDING_KEY_IMPLEMENTATION = Path(
    "products/zomlang/compiler/identity/key/import-binding-key.cc"
)
IDENTITY_CMAKE = Path("products/zomlang/compiler/identity/CMakeLists.txt")
INVENTORY = Path("products/zomlang/compiler/binder/definition-inventory.cc")
INVENTORY_HEADER = Path("products/zomlang/compiler/binder/definition-inventory.h")
BINDING_INPUT = Path("products/zomlang/compiler/binder/binding-input.h")
MODULE_RESOLUTION = Path("products/zomlang/compiler/binder/module-resolution.h")
MODULE_RESOLUTION_IMPLEMENTATION = Path(
    "products/zomlang/compiler/binder/module-resolution.cc"
)
COMPILER_SESSION = Path("products/zomlang/compiler/driver/compiler-session.cc")
CRATE_GRAPH = Path("products/zomlang/compiler/driver/crate-graph.h")
CRATE_GRAPH_IMPLEMENTATION = Path("products/zomlang/compiler/driver/crate-graph.cc")
PACKAGE_COMPILATION_REQUEST = Path(
    "products/zomlang/compiler/driver/package/package-compilation-request.h"
)
PACKAGE_COMPILATION_REQUEST_IMPLEMENTATION = Path(
    "products/zomlang/compiler/driver/package/package-compilation-request.cc"
)
SEMANTIC_TYPE_STORE = Path("products/zomlang/compiler/type/semantic-type-store.h")
SEMANTIC_TYPE_KEY = Path("products/zomlang/compiler/type/semantic-type-key.h")
SCALAR_LITERAL_FACTS = Path("products/zomlang/compiler/checker/scalar-literal-facts.h")
SCALAR_LITERAL_FACTS_IMPLEMENTATION = Path(
    "products/zomlang/compiler/checker/scalar-literal-facts.cc"
)
CHECKER_DIAGNOSTIC_ADAPTER = Path(
    "products/zomlang/compiler/checker/checker-diagnostic-adapter.h"
)
CHECKER_DIAGNOSTIC_ADAPTER_IMPLEMENTATION = Path(
    "products/zomlang/compiler/checker/checker-diagnostic-adapter.cc"
)
BORROW_INTERFACE = Path("products/zomlang/compiler/checker/borrow-interface.h")
BORROW_INTERFACE_IMPLEMENTATION = Path(
    "products/zomlang/compiler/checker/borrow-interface.cc"
)
HEADER_SYNTAX_SCHEMA = Path(
    "products/zomlang/compiler/identity/canonical/canonical-header-syntax-schema.yml"
)
HEADER_SYNTAX_DEFINITION = Path(
    "products/zomlang/compiler/identity/canonical/canonical-header-syntax-schema.def"
)
HEADER_SYNTAX_GENERATOR = Path("scripts/generate-canonical-header-syntax-schema.py")
CONFORMANCE_CMAKE = Path("products/zomlang/tests/conformance/CMakeLists.txt")
CANONICAL_HEADER_TYPE = Path("products/zomlang/compiler/identity/canonical/header-type.h")
CANONICAL_HEADER_TYPE_CORE = Path(
    "products/zomlang/compiler/identity/canonical/header-type-core.cc"
)
CANONICAL_HEADER_TYPE_COMPOUND = Path(
    "products/zomlang/compiler/identity/canonical/header-type-compound.cc"
)
CANONICAL_HEADER_TYPE_RECORDS = Path(
    "products/zomlang/compiler/identity/canonical/header-type-records.cc"
)
CANONICAL_HEADER_TYPE_ENCODE = Path(
    "products/zomlang/compiler/identity/canonical/header-type-encode.cc"
)
CANONICAL_HEADER_TYPE_TEST = Path(
    "products/zomlang/tests/unittests/compiler/identity/canonical/header-type-test.cc"
)
CANONICAL_HEADER_TYPE_PRODUCER = Path(
    "products/zomlang/compiler/binder/canonical-header-type-producer.h"
)
CANONICAL_HEADER_TYPE_PRODUCER_IMPLEMENTATION = Path(
    "products/zomlang/compiler/binder/canonical-header-type-producer.cc"
)
CANONICAL_HEADER_TYPE_PRODUCER_TEST = Path(
    "products/zomlang/tests/unittests/compiler/binder/canonical-header-type-producer-test.cc"
)
CANONICAL_DEFINITION_HEADER_PRODUCER = Path(
    "products/zomlang/compiler/binder/canonical-definition-header-producer.h"
)
CANONICAL_DEFINITION_HEADER_PRODUCER_IMPLEMENTATION = Path(
    "products/zomlang/compiler/binder/canonical-definition-header-producer.cc"
)
CANONICAL_DEFINITION_HEADER_PRODUCER_TEST = Path(
    "products/zomlang/tests/unittests/compiler/binder/canonical-definition-header-producer-test.cc"
)
CANONICAL_IMPL_HEADER_PRODUCER = Path(
    "products/zomlang/compiler/binder/canonical-impl-header-producer.h"
)
CANONICAL_IMPL_HEADER_PRODUCER_IMPLEMENTATION = Path(
    "products/zomlang/compiler/binder/canonical-impl-header-producer.cc"
)
CANONICAL_IMPL_HEADER_PRODUCER_TEST = Path(
    "products/zomlang/tests/unittests/compiler/binder/canonical-impl-header-producer-test.cc"
)
BINDER_CMAKE = Path("products/zomlang/compiler/binder/CMakeLists.txt")
BINDER_TEST_CMAKE = Path("products/zomlang/tests/unittests/compiler/binder/CMakeLists.txt")
BINDING_VERIFIER = Path("products/zomlang/compiler/binder/stable/header/verifier.cc")
CANONICAL_OVERLOAD_HEADER = Path(
    "products/zomlang/compiler/identity/canonical/overload-header.h"
)
CANONICAL_OVERLOAD_HEADER_IMPLEMENTATION = Path(
    "products/zomlang/compiler/identity/canonical/overload-header.cc"
)
CANONICAL_OVERLOAD_HEADER_TEST = Path(
    "products/zomlang/tests/unittests/compiler/identity/canonical/overload-header-test.cc"
)
OVERLOAD_HEADER_DIGEST = Path("products/zomlang/compiler/identity/crypto/overload-header-digest.h")
OVERLOAD_HEADER_DIGEST_IMPLEMENTATION = Path(
    "products/zomlang/compiler/identity/crypto/overload-header-digest.cc"
)
OVERLOAD_HEADER_DIGEST_TEST = Path(
    "products/zomlang/tests/unittests/compiler/identity/crypto/overload-header-digest-test.cc"
)
CANONICAL_IMPL_HEADER = Path("products/zomlang/compiler/identity/canonical/impl-header.h")
CANONICAL_IMPL_HEADER_IMPLEMENTATION = Path(
    "products/zomlang/compiler/identity/canonical/impl-header.cc"
)
CANONICAL_IMPL_HEADER_TEST = Path(
    "products/zomlang/tests/unittests/compiler/identity/canonical/impl-header-test.cc"
)
MODULE_RESOLUTION_KEY_TEST = Path(
    "products/zomlang/tests/unittests/compiler/identity/key/module-resolution-key-test.cc"
)
SEMANTIC_IMPORT_BINDING_KEY_TEST = Path(
    "products/zomlang/tests/unittests/compiler/identity/key/import-binding-key-test.cc"
)
IDENTITY_TEST_CMAKE = Path("products/zomlang/tests/unittests/compiler/identity/CMakeLists.txt")
PARSER_ROOT = Path("products/zomlang/compiler/parser")
COMPILER_ROOT = ROOT / "products" / "zomlang" / "compiler"

SCHEMA_CONTEXTUAL_PRODUCERS = {
    "ModuleDeclaration",
    "ImportDeclaration",
    "ExportDeclaration",
    "ImportSpecifier",
    "ExportSpecifier",
    "GenericTypeParam",
    "FunctionExpression",
    "LambdaExpression",
    "LetStmt",
    "ForInStatement",
    "MatchArmStmt",
    "VariableDeclarator",
    "RestPattern",
    "BindingPattern",
    "IdentifierPattern",
    "PatternProperty",
}

NON_DEFINITION_IDENTITIES = {"Module", "Impl"}
POINTER_IDENTITY_PATTERN = re.compile(
    r"reinterpret_cast\s*<\s*uintptr_t\s*>\s*\(\s*&|\bkScopeIdMask\b|\bkNoBufferId\b"
)


def relative(path: Path) -> str:
    return str(path.relative_to(ROOT))


IDENTITY_ROOT = Path("products/zomlang/compiler/identity")


def identity_source(path: Path) -> str:
    return str(path.relative_to(IDENTITY_ROOT))


@functools.lru_cache(maxsize=None)
def repository_text(path: Path) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


@functools.lru_cache(maxsize=1)
def compiler_source_files() -> tuple[Path, ...]:
    files: list[Path] = []
    for suffix in ("*.h", "*.cc"):
        files.extend(path.relative_to(ROOT) for path in COMPILER_ROOT.rglob(suffix))
    return tuple(sorted(files))


def read_text(path: Path, overrides: dict[Path, str]) -> str:
    if path in overrides:
        return overrides[path]
    return repository_text(path)


def load_manifest() -> dict[str, object]:
    with (ROOT / MANIFEST).open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict):
        raise ValueError(f"{MANIFEST} must contain a JSON object")
    return data


def load_schema_variants(overrides: dict[Path, str]) -> dict[str, set[str]]:
    text = read_text(SCHEMA, overrides)
    variants: dict[str, set[str]] = {}
    blocks = re.split(r"(?=^  - id:\s*)", text, flags=re.MULTILINE)
    for block in blocks:
        name_match = re.search(r"^    name:\s*([A-Za-z][A-Za-z0-9_]*)\s*$", block, re.MULTILINE)
        if name_match is None:
            continue
        bases_match = re.search(r"^    bases:\s*\[([^]]*)\]", block, re.MULTILINE)
        bases = set()
        if bases_match is not None:
            bases = {item.strip() for item in bases_match.group(1).split(",") if item.strip()}
        variants[name_match.group(1)] = bases
    return variants


def enum_members(text: str, enum_name: str) -> set[str]:
    match = re.search(rf"enum\s+class\s+{enum_name}\b[^{{]*\{{(.*?)\}};", text, re.DOTALL)
    if match is None:
        return set()
    return set(re.findall(r"\b([A-Za-z][A-Za-z0-9_]*)\s*(?:=|,|$)", match.group(1)))


def check_manifest_shape(manifest: dict[str, object], errors: list[str]) -> None:
    for key in (
        "producers",
        "no_identity",
        "expansion_producers",
        "pointer_identity_allowlist",
    ):
        if key not in manifest:
            errors.append(f"{MANIFEST}: missing required key {key}")


def check_schema_coverage(
    manifest: dict[str, object], variants: dict[str, set[str]], errors: list[str]
) -> None:
    producers = manifest.get("producers", {})
    no_identity = manifest.get("no_identity", {})
    if not isinstance(producers, dict) or not isinstance(no_identity, dict):
        return

    accounted = set(producers) | set(no_identity)
    declaration_nodes = {
        name
        for name, bases in variants.items()
        if "NamedDeclaration" in bases or "DeclarationStatement" in bases
    }
    declaration_nodes |= SCHEMA_CONTEXTUAL_PRODUCERS

    for node in sorted(declaration_nodes - accounted):
        errors.append(f"{SCHEMA}: declaration-bearing node {node} has no identity rule")
    for node in sorted(accounted - set(variants)):
        errors.append(f"{MANIFEST}: identity rule names unknown schema node {node}")


def check_live_producers(
    manifest: dict[str, object], overrides: dict[Path, str], errors: list[str]
) -> None:
    producers = manifest.get("producers", {})
    if not isinstance(producers, dict):
        return
    inventory_text = read_text(INVENTORY, overrides)
    definition_text = read_text(DEFINITION_KEY, overrides)
    definition_kinds = enum_members(definition_text, "DefinitionKind")
    anonymous_roles = enum_members(
        read_text(INVENTORY_HEADER, overrides), "AnonymousSyntaxRole"
    )

    for node, raw_rule in sorted(producers.items()):
        if not isinstance(raw_rule, dict):
            errors.append(f"{MANIFEST}: producer {node} must be an object")
            continue
        parser_name = raw_rule.get("parser")
        if not isinstance(parser_name, str):
            errors.append(f"{MANIFEST}: producer {node} has no parser source")
        else:
            parser_path = PARSER_ROOT / parser_name
            try:
                parser_text = read_text(parser_path, overrides)
            except FileNotFoundError:
                errors.append(f"{parser_path}: parser source for {node} does not exist")
            else:
                if re.search(rf"\bmake{re.escape(node)}\s*\(", parser_text) is None:
                    errors.append(f"{parser_path}: live parser producer make{node} is missing")

        marker = f"case ast::SyntaxKind::{node}:"
        if marker not in inventory_text:
            errors.append(f"{INVENTORY}: prebinding inventory handler for {node} is missing")

        identities = raw_rule.get("identities")
        if not isinstance(identities, list) or not identities:
            errors.append(f"{MANIFEST}: producer {node} must name at least one identity")
        else:
            for identity in identities:
                if identity not in definition_kinds and identity not in NON_DEFINITION_IDENTITIES:
                    errors.append(f"{MANIFEST}: producer {node} names unknown identity {identity}")

        anonymous_role = raw_rule.get("anonymous_role")
        if anonymous_role is not None and anonymous_role not in anonymous_roles:
            errors.append(
                f"{MANIFEST}: producer {node} names unknown anonymous role {anonymous_role}"
            )


def check_no_post_parse_expansion(
    manifest: dict[str, object], overrides: dict[Path, str], errors: list[str]
) -> None:
    expansion_producers = manifest.get("expansion_producers")
    if expansion_producers != []:
        errors.append(f"{MANIFEST}: current compiler must have an empty expansion producer set")

    producers = manifest.get("producers", {})
    if not isinstance(producers, dict):
        return
    producer_names = tuple(sorted(producers))
    maker = re.compile(r"\bmake(" + "|".join(re.escape(name) for name in producer_names) + r")\s*\(")
    matches = {Path(path): values for path, values in baseline_post_parse_expansion_matches(producer_names)}
    for relative_path, text in overrides.items():
        if relative_path.suffix != ".cc" or relative_path.parent == PARSER_ROOT:
            continue
        matches[relative_path] = tuple(match.group(1) for match in maker.finditer(text))
    for relative_path in sorted(matches):
        for producer_name in matches[relative_path]:
            errors.append(
                f"{relative_path}: post-parse semantic producer make{producer_name} is forbidden"
            )


@functools.lru_cache(maxsize=None)
def baseline_post_parse_expansion_matches(
    producer_names: tuple[str, ...],
) -> tuple[tuple[str, tuple[str, ...]], ...]:
    maker = re.compile(r"\bmake(" + "|".join(re.escape(name) for name in producer_names) + r")\s*\(")
    matches: list[tuple[str, tuple[str, ...]]] = []
    for relative_path in compiler_source_files():
        if relative_path.suffix != ".cc" or relative_path.parent == PARSER_ROOT:
            continue
        values = tuple(match.group(1) for match in maker.finditer(repository_text(relative_path)))
        if values:
            matches.append((str(relative_path), values))
    return tuple(matches)


def matching_files(pattern: re.Pattern[str], overrides: dict[Path, str]) -> set[str]:
    matches = set(baseline_matching_files(pattern.pattern, pattern.flags))
    for relative_path, text in overrides.items():
        if relative_path not in compiler_source_files():
            continue
        if pattern.search(text):
            matches.add(str(relative_path))
        else:
            matches.discard(str(relative_path))
    return matches


@functools.lru_cache(maxsize=None)
def baseline_matching_files(pattern_text: str, flags: int) -> frozenset[str]:
    pattern = re.compile(pattern_text, flags)
    return frozenset(
        str(relative_path)
        for relative_path in compiler_source_files()
        if pattern.search(repository_text(relative_path))
    )


def matching_occurrence_paths(pattern: re.Pattern[str], overrides: dict[Path, str]) -> tuple[Path, ...]:
    paths = [Path(path) for path in baseline_matching_occurrence_paths(pattern.pattern, pattern.flags)]
    for relative_path, text in overrides.items():
        if relative_path not in compiler_source_files():
            continue
        paths = [path for path in paths if path != relative_path]
        paths.extend(relative_path for _ in pattern.finditer(text))
    return tuple(paths)


@functools.lru_cache(maxsize=None)
def baseline_matching_occurrence_paths(pattern_text: str, flags: int) -> tuple[str, ...]:
    pattern = re.compile(pattern_text, flags)
    return tuple(
        str(relative_path)
        for relative_path in compiler_source_files()
        for _ in pattern.finditer(repository_text(relative_path))
    )


def function_body(text: str, marker: str) -> str | None:
    marker_offset = text.find(marker)
    if marker_offset < 0:
        return None
    body_start = text.find("{", marker_offset + len(marker))
    if body_start < 0:
        return None

    depth = 0
    for offset in range(body_start, len(text)):
        if text[offset] == "{":
            depth += 1
        elif text[offset] == "}":
            depth -= 1
            if depth == 0:
                return text[body_start + 1 : offset]
    return None


def declaration_body(text: str, marker: str) -> str | None:
    marker_offset = text.find(marker)
    if marker_offset < 0:
        return None
    body_start = text.find("{", marker_offset + len(marker))
    if body_start < 0:
        return None

    depth = 0
    for offset in range(body_start, len(text)):
        if text[offset] == "{":
            depth += 1
        elif text[offset] == "}":
            depth -= 1
            if depth == 0:
                return text[body_start + 1 : offset]
    return None


def data_member_declarations(body: str) -> tuple[str, ...]:
    declarations: list[str] = []
    for match in re.finditer(r"^[ \t]+([^\n;{}()]+;)\s*$", body, re.MULTILINE):
        declarations.append(" ".join(match.group(1).split()))
    return tuple(declarations)


def tagged_enum_members(text: str, enum_name: str) -> tuple[tuple[str, str], ...] | None:
    match = re.search(rf"enum\s+class\s+{enum_name}\b[^{{]*\{{(.*?)\}};", text, re.DOTALL)
    if match is None:
        return None

    members: list[tuple[str, str]] = []
    for item in match.group(1).split(","):
        normalized = " ".join(item.split())
        if not normalized:
            continue
        member = re.fullmatch(r"([A-Za-z][A-Za-z0-9_]*)\s*=\s*(0x[0-9A-Fa-f]+|[0-9]+)", normalized)
        if member is None:
            return ()
        members.append((member.group(1), member.group(2).lower()))
    return tuple(members)


def check_ordered_markers(
    body: str,
    ordered_markers: tuple[str, ...],
    path: Path,
    description: str,
    errors: list[str],
) -> None:
    offsets = [body.find(marker) for marker in ordered_markers]
    for marker, offset in zip(ordered_markers, offsets):
        if offset < 0:
            errors.append(f"{path}: {description} is missing {marker}")
    if all(offset >= 0 for offset in offsets) and offsets != sorted(offsets):
        errors.append(f"{path}: {description} must preserve canonical field order")


def check_ordered_function_markers(
    text: str,
    function_marker: str,
    ordered_markers: tuple[str, ...],
    description: str,
    errors: list[str],
) -> None:
    body = function_body(text, function_marker)
    if body is None:
        errors.append(f"{COMPILER_SESSION}: missing {description} function body")
        return

    offsets = [body.find(marker) for marker in ordered_markers]
    for marker, offset in zip(ordered_markers, offsets):
        if offset < 0:
            errors.append(f"{COMPILER_SESSION}: {description} is missing {marker}")
    if all(offset >= 0 for offset in offsets) and offsets != sorted(offsets):
        errors.append(
            f"{COMPILER_SESSION}: {description} must order " + " before ".join(ordered_markers)
        )


def check_pointer_identity_allowlist(
    manifest: dict[str, object], overrides: dict[Path, str], errors: list[str]
) -> None:
    key = "pointer_identity_allowlist"
    configured = manifest.get(key, [])
    if not isinstance(configured, list) or not all(isinstance(item, str) for item in configured):
        errors.append(f"{MANIFEST}: {key} must be a string list")
        return
    expected = set(configured)
    actual = matching_files(POINTER_IDENTITY_PATTERN, overrides)
    for path in sorted(actual - expected):
        errors.append(f"{path}: unallowlisted pointer-derived identity surface")
    for path in sorted(expected - actual):
        errors.append(f"{MANIFEST}: stale pointer-derived identity surface allowlist entry {path}")


def check_header_wire_inventory(
    overrides: dict[Path, str], errors: list[str]
) -> None:
    schema_text = read_text(HEADER_SYNTAX_SCHEMA, overrides)
    definition_text = read_text(HEADER_SYNTAX_DEFINITION, overrides)
    cmake_text = read_text(CONFORMANCE_CMAKE, overrides)

    required_cmake_patterns = (
        (
            r"NAME canonical-header-syntax-schema\b.*?"
            r"generate-canonical-header-syntax-schema\.py.*?--check",
            "canonical header schema check",
        ),
        (
            r"NAME canonical-header-syntax-schema-negative\b.*?"
            r"generate-canonical-header-syntax-schema\.py.*?--self-test",
            "canonical header schema self-test",
        ),
    )
    for pattern, description in required_cmake_patterns:
        if re.search(pattern, cmake_text, re.DOTALL) is None:
            errors.append(f"{CONFORMANCE_CMAKE}: missing {description} registration")

    with tempfile.TemporaryDirectory(prefix="zom-identity-header-schema-") as directory:
        temporary_root = Path(directory)
        schema_path = temporary_root / HEADER_SYNTAX_SCHEMA.name
        generated_path = temporary_root / HEADER_SYNTAX_DEFINITION.name
        schema_path.write_text(schema_text, encoding="utf-8")
        result = subprocess.run(
            [
                sys.executable,
                str(ROOT / HEADER_SYNTAX_GENERATOR),
                "--schema",
                str(schema_path),
                "--output",
                str(generated_path),
            ],
            cwd=ROOT,
            capture_output=True,
            check=False,
            text=True,
        )
        if result.returncode != 0:
            detail = result.stderr.strip() or result.stdout.strip() or "unknown generator error"
            errors.append(
                f"{HEADER_SYNTAX_SCHEMA}: canonical header schema generation failed: {detail}"
            )
            return
        generated_text = generated_path.read_text(encoding="utf-8")
        if generated_text != definition_text:
            errors.append(
                f"{HEADER_SYNTAX_DEFINITION}: generated canonical header definition is stale"
            )


def check_canonical_header_type_inventory(
    overrides: dict[Path, str], errors: list[str]
) -> None:
    header = read_text(CANONICAL_HEADER_TYPE, overrides)
    core = read_text(CANONICAL_HEADER_TYPE_CORE, overrides)
    compound = read_text(CANONICAL_HEADER_TYPE_COMPOUND, overrides)
    records = read_text(CANONICAL_HEADER_TYPE_RECORDS, overrides)
    encode = read_text(CANONICAL_HEADER_TYPE_ENCODE, overrides)
    identity_cmake = read_text(IDENTITY_CMAKE, overrides)
    test = read_text(CANONICAL_HEADER_TYPE_TEST, overrides)

    for source in (
        CANONICAL_HEADER_TYPE_COMPOUND,
        CANONICAL_HEADER_TYPE_CORE,
        CANONICAL_HEADER_TYPE_ENCODE,
        CANONICAL_HEADER_TYPE_RECORDS,
    ):
        marker = f"${{CMAKE_CURRENT_SOURCE_DIR}}/{identity_source(source)}"
        if marker not in identity_cmake:
            errors.append(f"{IDENTITY_CMAKE}: missing canonical header type source {identity_source(source)}")

    for marker in (
        "enum class CanonicalHeaderTypeSyntaxKind : uint8_t",
        "static zc::Maybe<CanonicalHeaderTypeSyntax> function(",
        "static zc::Maybe<CanonicalHeaderTypeSyntax> unionOf(",
        "static zc::Maybe<CanonicalHeaderTypeSyntax> intersectionOf(",
        "static CanonicalHeaderTypeSyntax dynamicArray(",
        "static CanonicalHeaderTypeSyntax slice(",
        "static CanonicalHeaderTypeSyntax dynamic(",
        "CanonicalHeaderTypeSyntax clone() const;",
        "void encode(CanonicalEncoder& encoder) const;",
        "zc::Array<uint8_t> encode() const;",
    ):
        if marker not in header:
            errors.append(f"{CANONICAL_HEADER_TYPE}: missing public canonical type marker {marker}")

    for marker in (
        "appendFlattened(CanonicalHeaderTypeSyntaxKind::Union",
        "appendFlattened(CanonicalHeaderTypeSyntaxKind::Intersection",
        "if (normalized.size() == 1) { return zc::mv(normalized[0]); }",
        "values = sortUnique(zc::mv(values));",
    ):
        if marker not in core:
            errors.append(f"{CANONICAL_HEADER_TYPE_CORE}: missing normalization marker {marker}")

    for marker in (
        "if (depth != 0x01 && depth != 0x02) { return zc::none; }",
        "detail::ObjectTypeData{sortUnique(zc::mv(members))}",
        "sortUnique(zc::mv(markers))",
        "sortUnique(zc::mv(associatedBindings))",
    ):
        if marker not in compound:
            errors.append(f"{CANONICAL_HEADER_TYPE_COMPOUND}: missing compound marker {marker}")

    for marker in (
        "impl->name.encode(encoder);",
        "encoder.encodeSequenceSize(impl->arguments.size());",
        "impl->type.encode(encoder);",
        "encoder.encodeBool(impl->isMutable);",
        "encoder.encodeBool(impl->isOptional);",
    ):
        if marker not in records:
            errors.append(f"{CANONICAL_HEADER_TYPE_RECORDS}: missing record codec marker {marker}")
    if "encodeByteString" in records or "encodeByteString" in encode:
        errors.append(
            f"{CANONICAL_HEADER_TYPE_ENCODE}: nested canonical type records must encode inline"
        )

    if "encoder.encodeUint8(static_cast<uint8_t>(typeKind));" not in encode:
        errors.append(f"{CANONICAL_HEADER_TYPE_ENCODE}: canonical type tag must encode first")
    for variant in (
        "Named",
        "Predefined",
        "Function",
        "Union",
        "Intersection",
        "FixedArray",
        "DynamicArray",
        "Slice",
        "Optional",
        "Reference",
        "RawPointer",
        "TypeQuery",
        "Object",
        "Tuple",
        "AssociatedProjection",
        "Dynamic",
    ):
        marker = f"case CanonicalHeaderTypeSyntaxKind::{variant}:"
        if marker not in encode:
            errors.append(f"{CANONICAL_HEADER_TYPE_ENCODE}: missing {variant} codec case")
    for marker in (
        "encodeSequence(encoder, value.parameters.asPtr());",
        "value.result.encode(encoder);",
        "encoder.encodeSome();",
        "encoder.encodeUint64(value.length);",
        "encoder.encodeUint8(value.depth);",
        "value.member.encode(encoder);",
        "value.principal.encode(encoder);",
        "encodeSequence(encoder, value.markers.asPtr());",
        "encodeSequence(encoder, value.associatedBindings.asPtr());",
    ):
        if marker not in encode:
            errors.append(f"{CANONICAL_HEADER_TYPE_ENCODE}: missing field codec marker {marker}")

    for marker in (
        'ZC_TEST("CanonicalHeaderTypeSyntax passes fixed vectors for all sixteen tags and fields")',
        'ZC_TEST("CanonicalHeaderTypeSyntax rejects invalid enums depth empty sets and empty raises")',
        'ZC_TEST("CanonicalHeaderTypeSyntax normalizes unions intersections objects and dynamic sets")',
        'ZC_TEST("CanonicalHeaderTypeSyntax preserves ordered children clone accessors and array identity")',
        'expectHex(value, "030000000000000000020f01000000000000000202010202"_zc);',
        "ZC_REQUIRE(members.size() == 3);",
        'expectHex(dynamicArray, "070201"_zc);',
        'expectHex(slice, "080201"_zc);',
    ):
        if marker not in test:
            errors.append(f"{CANONICAL_HEADER_TYPE_TEST}: missing canonical type test marker {marker}")


def check_canonical_header_type_producer(
    overrides: dict[Path, str], errors: list[str]
) -> None:
    header = read_text(CANONICAL_HEADER_TYPE_PRODUCER, overrides)
    implementation = read_text(CANONICAL_HEADER_TYPE_PRODUCER_IMPLEMENTATION, overrides)
    test = read_text(CANONICAL_HEADER_TYPE_PRODUCER_TEST, overrides)
    binder_cmake = read_text(BINDER_CMAKE, overrides)
    binder_test_cmake = read_text(BINDER_TEST_CMAKE, overrides)
    verifier = read_text(BINDING_VERIFIER, overrides)

    frame = declaration_body(header, "struct CanonicalGenericBinderFrame final")
    if frame is None or data_member_declarations(frame) != ("ast::NodeId genericParameters;",):
        errors.append(
            f"{CANONICAL_HEADER_TYPE_PRODUCER}: generic binder frame must contain exactly one AST binder node"
        )
    for marker in (
        "class CanonicalHeaderTypeProducer final",
        "static zc::Maybe<CanonicalHeaderSyntaxFailure> validateBinderStack(",
        "static CanonicalHeaderTypeProduction produceType(",
        "zc::ArrayPtr<const CanonicalGenericBinderFrame> binders",
    ):
        if marker not in header:
            errors.append(
                f"{CANONICAL_HEADER_TYPE_PRODUCER}: missing canonical type producer marker {marker}"
            )

    variant_markers = (
        "NamedTypeExpr",
        "PredefinedTypeExpr",
        "FunctionTypeExpr",
        "UnionTypeExpr",
        "IntersectionTypeExpr",
        "FixedArrayTypeExpr",
        "ArrayTypeExpr",
        "SliceArrayTypeExpr",
        "OptionalTypeExpr",
        "ReferenceTypeExpr",
        "RawPointerTypeExpr",
        "TypeQueryExpr",
        "ObjectTypeExpr",
        "TupleTypeExpr",
        "AssociatedTypeProjectionExpr",
        "DynTypeExpr",
    )
    for variant in variant_markers:
        if f"case ast::SyntaxKind::{variant}:" not in implementation:
            errors.append(
                f"{CANONICAL_HEADER_TYPE_PRODUCER_IMPLEMENTATION}: missing AST mapping for {variant}"
            )

    for marker, description in (
        (
            "for (size_t depth = 0; depth < binders.size(); ++depth)",
            "search lexical generic binders by stable-owner depth",
        ),
        (
            "CanonicalNameRoot::generic(static_cast<uint32_t>(depth),",
            "encode generic depth and ordinal",
        ),
        ("if (rootTag == 0)", "restrict generic lookup to relative names"),
        ("else if (rootTag != 1)", "reject unknown module-path roots"),
        ("if (leading != 0)", "reject producerless attribute-path roots"),
        (
            "value > (UINT64_MAX - digit) / base",
            "reject fixed-array length overflow",
        ),
        (
            "appendRaises(alternative, result)",
            "recursively flatten raises union syntax",
        ),
    ):
        if marker not in implementation:
            errors.append(
                f"{CANONICAL_HEADER_TYPE_PRODUCER_IMPLEMENTATION}: producer must {description}"
            )

    if "${CMAKE_CURRENT_SOURCE_DIR}/canonical-header-type-producer.cc" not in binder_cmake:
        errors.append(f"{BINDER_CMAKE}: missing canonical header type producer source")
    if not re.search(
        r'add_ztest_unit_test\("canonical-header-type-producer-test"\s+'
        r'"canonical-header-type-producer-test\.cc"',
        binder_test_cmake,
    ):
        errors.append(f"{BINDER_TEST_CMAKE}: missing canonical header type producer test target")

    for marker in (
        'ZC_TEST("CanonicalHeaderTypeProducer alpha-normalizes current generic binder names")',
        'ZC_TEST("CanonicalHeaderTypeProducer reserves empty owner depth and preserves absolute roots")',
        'ZC_TEST("CanonicalHeaderTypeProducer keeps dynamic arrays and slices unequal")',
        'ZC_TEST("CanonicalHeaderTypeProducer covers every RFC 0018 type variant")',
        'ZC_TEST("CanonicalHeaderTypeProducer evaluates fixed array lengths and rejects non-literals")',
        'ZC_TEST("CanonicalHeaderTypeProducer flattens union members and function raises")',
        'ZC_TEST("CanonicalHeaderTypeProducer resolves duplicate generic names to the first ordinal")',
        '"0103000000000000000000000000000000000000000000000000"_zc',
    ):
        if marker not in test:
            errors.append(
                f"{CANONICAL_HEADER_TYPE_PRODUCER_TEST}: missing producer test marker {marker}"
            )

    if "CanonicalHeaderTypeProducer" in verifier:
        errors.append(
            f"{BINDING_VERIFIER}: independent verifier must not call the canonical type producer"
        )


def check_canonical_definition_header_producer(
    overrides: dict[Path, str], errors: list[str]
) -> None:
    header = read_text(CANONICAL_DEFINITION_HEADER_PRODUCER, overrides)
    implementation = read_text(CANONICAL_DEFINITION_HEADER_PRODUCER_IMPLEMENTATION, overrides)
    test = read_text(CANONICAL_DEFINITION_HEADER_PRODUCER_TEST, overrides)
    binder_cmake = read_text(BINDER_CMAKE, overrides)
    binder_test_cmake = read_text(BINDER_TEST_CMAKE, overrides)
    verifier = read_text(BINDING_VERIFIER, overrides)

    for marker in (
        "class CanonicalDefinitionHeaderProducer final",
        "static CanonicalDefinitionHeaderProduction produce(",
        "const DefinitionInventoryEntry& definition",
        "zc::ArrayPtr<const CanonicalGenericBinderFrame> enclosingBinders",
    ):
        if marker not in header:
            errors.append(
                f"{CANONICAL_DEFINITION_HEADER_PRODUCER}: missing definition producer marker {marker}"
            )

    for variant in ("FunctionDecl", "ExternDecl", "MethodDecl", "ConstructorDecl"):
        if f"case ast::SyntaxKind::{variant}:" not in implementation:
            errors.append(
                f"{CANONICAL_DEFINITION_HEADER_PRODUCER_IMPLEMENTATION}: missing callable mapping for {variant}"
            )
    for marker, description in (
        (
            "frames.add(CanonicalGenericBinderFrame{syntax.genericParameters});",
            "reserve the callable binder at depth zero",
        ),
        (
            "CanonicalHeaderTypeProducer::validateBinderStack(tree, frames.asPtr())",
            "validate the complete callable binder stack independently of type use",
        ),
        (
            "tree.ident(name) != tree.ident(definition.declaredName)",
            "require inventory and header name equality",
        ),
        (
            "definition.kind != identity::DefinitionKind::Function",
            "validate function inventory kinds",
        ),
        (
            "definition.kind != identity::DefinitionKind::Method",
            "validate method inventory kinds",
        ),
        (
            "definition.kind != identity::DefinitionKind::Constructor",
            "validate constructor inventory kinds",
        ),
        (
            "CanonicalBoundObligation::from(genericSubject(ordinal)",
            "collect inline generic bounds",
        ),
        ("appendWhere(", "merge where-clause obligations"),
        (
            'tree.ident(names[2]) == "move"_zc',
            "recognize the exact move-receiver attribute",
        ),
        ("methodMode == 1", "reject static receivers"),
        ("methodMode == 2 && !foundReceiver", "reject receiverless mutating methods"),
        (
            "OverloadHeader::from(",
            "admit the complete canonical overload header",
        ),
        (
            "OverloadHeaderAuthority::from(zc::mv(value))",
            "retain the complete header authority",
        ),
    ):
        if marker not in implementation:
            errors.append(
                f"{CANONICAL_DEFINITION_HEADER_PRODUCER_IMPLEMENTATION}: producer must {description}"
            )

    if "${CMAKE_CURRENT_SOURCE_DIR}/canonical-definition-header-producer.cc" not in binder_cmake:
        errors.append(f"{BINDER_CMAKE}: missing canonical definition header producer source")
    if not re.search(
        r'add_ztest_unit_test\("canonical-definition-header-producer-test"\s+'
        r'"canonical-definition-header-producer-test\.cc"',
        binder_test_cmake,
    ):
        errors.append(
            f"{BINDER_TEST_CMAKE}: missing canonical definition header producer test target"
        )
    for marker in (
        'ZC_TEST("CanonicalDefinitionHeaderProducer alpha-normalizes callable generic names")',
        'ZC_TEST("CanonicalDefinitionHeaderProducer merges inline and where obligations")',
        'ZC_TEST("CanonicalDefinitionHeaderProducer normalizes receivers and removes them from parameters")',
        'ZC_TEST("CanonicalDefinitionHeaderProducer admits exact move and mutable-reference receivers")',
        'ZC_TEST("CanonicalDefinitionHeaderProducer preserves callable kind result and ABI contracts")',
        'ZC_TEST("CanonicalDefinitionHeaderProducer rejects inventory kind and name mismatches")',
        'ZC_TEST("CanonicalDefinitionHeaderProducer admits duplicate unused generic names")',
    ):
        if marker not in test:
            errors.append(
                f"{CANONICAL_DEFINITION_HEADER_PRODUCER_TEST}: missing definition producer test marker {marker}"
            )

    if "CanonicalDefinitionHeaderProducer" in verifier:
        errors.append(
            f"{BINDING_VERIFIER}: independent verifier must not call the canonical definition producer"
        )


def check_canonical_impl_header_producer(
    overrides: dict[Path, str], errors: list[str]
) -> None:
    header = read_text(CANONICAL_IMPL_HEADER_PRODUCER, overrides)
    implementation = read_text(CANONICAL_IMPL_HEADER_PRODUCER_IMPLEMENTATION, overrides)
    test = read_text(CANONICAL_IMPL_HEADER_PRODUCER_TEST, overrides)
    binder_cmake = read_text(BINDER_CMAKE, overrides)
    binder_test_cmake = read_text(BINDER_TEST_CMAKE, overrides)
    verifier = read_text(BINDING_VERIFIER, overrides)

    for marker in (
        "class CanonicalImplHeaderProducer final",
        "static CanonicalImplHeaderProduction produce(",
        "const ImplInventoryEntry& implementation",
        "zc::ArrayPtr<const CanonicalGenericBinderFrame> enclosingBinders",
    ):
        if marker not in header:
            errors.append(
                f"{CANONICAL_IMPL_HEADER_PRODUCER}: missing impl producer marker {marker}"
            )

    for marker, description in (
        ("syntax.kind == ast::SyntaxKind::StandaloneImplDecl", "map standalone impl syntax"),
        ("syntax.kind == ast::SyntaxKind::MarkerImpl", "map marker impl syntax"),
        (
            "frames.add(CanonicalGenericBinderFrame{genericParameters});",
            "reserve the implementation binder at depth zero",
        ),
        (
            "CanonicalHeaderTypeProducer::validateBinderStack(tree, frames.asPtr())",
            "validate the complete implementation binder stack independently of type use",
        ),
        (
            "CanonicalTraitReference::from(named.name().clone(), zc::mv(arguments))",
            "retain a pure canonical trait syntax reference",
        ),
        (
            "polarity == ImplPolarity::Negative && safety == ImplSafety::Unsafe",
            "reject only the parser-invalid negative unsafe marker combination",
        ),
        (
            "CanonicalBoundObligation::from(genericSubject(ordinal)",
            "collect inline generic bounds",
        ),
        ("appendWhere(", "merge generic and implementation where obligations"),
        ("ImplHeader::from(", "admit the complete canonical impl header"),
    ):
        if marker not in implementation:
            errors.append(
                f"{CANONICAL_IMPL_HEADER_PRODUCER_IMPLEMENTATION}: producer must {description}"
            )

    if "${CMAKE_CURRENT_SOURCE_DIR}/canonical-impl-header-producer.cc" not in binder_cmake:
        errors.append(f"{BINDER_CMAKE}: missing canonical impl header producer source")
    if not re.search(
        r'add_ztest_unit_test\("canonical-impl-header-producer-test"\s+'
        r'"canonical-impl-header-producer-test\.cc"',
        binder_test_cmake,
    ):
        errors.append(f"{BINDER_TEST_CMAKE}: missing canonical impl header producer test target")

    for marker in (
        'ZC_TEST("CanonicalImplHeaderProducer alpha-normalizes implementation generic names")',
        'ZC_TEST("CanonicalImplHeaderProducer merges inline and where obligations")',
        'ZC_TEST("CanonicalImplHeaderProducer admits positive safe marker paths and exact tags")',
        'ZC_TEST("CanonicalImplHeaderProducer reserves an empty current binder depth")',
        'ZC_TEST("CanonicalImplHeaderProducer rejects generic traits and admits duplicate binder names")',
        "requireHeader(safeResult).safety() == identity::ImplSafety::Safe",
        "requireHeader(qualifiedResult).trait().name().suffix().size() == 2",
    ):
        if marker not in test:
            errors.append(
                f"{CANONICAL_IMPL_HEADER_PRODUCER_TEST}: missing impl producer test marker {marker}"
            )

    if "CanonicalImplHeaderProducer" in verifier:
        errors.append(
            f"{BINDING_VERIFIER}: independent verifier must not call the canonical impl producer"
        )


def check_canonical_overload_header_inventory(
    overrides: dict[Path, str], errors: list[str]
) -> None:
    header = read_text(CANONICAL_OVERLOAD_HEADER, overrides)
    implementation = read_text(CANONICAL_OVERLOAD_HEADER_IMPLEMENTATION, overrides)
    identity_cmake = read_text(IDENTITY_CMAKE, overrides)
    test = read_text(CANONICAL_OVERLOAD_HEADER_TEST, overrides)

    source_marker = (
        f"${{CMAKE_CURRENT_SOURCE_DIR}}/{identity_source(CANONICAL_OVERLOAD_HEADER_IMPLEMENTATION)}"
    )
    if source_marker not in identity_cmake:
        errors.append(
            f"{IDENTITY_CMAKE}: missing canonical overload source "
            f"{identity_source(CANONICAL_OVERLOAD_HEADER_IMPLEMENTATION)}"
        )

    for marker in (
        "class CanonicalCallableResult final",
        "class CanonicalGenericParameter final",
        "class CanonicalBoundObligation final",
        "class CanonicalCallableParameter final",
        "class OverloadHeader final",
        "static CanonicalCallableResult type(CanonicalHeaderTypeSyntax&& type);",
        "static zc::Maybe<OverloadHeader> from(",
        "OverloadHeader clone() const;",
        "zc::ArrayPtr<const CanonicalGenericParameter> genericParameters() const noexcept;",
        "zc::ArrayPtr<const CanonicalBoundObligation> obligations() const noexcept;",
        "zc::ArrayPtr<const CanonicalCallableParameter> parameters() const noexcept;",
        "void encode(CanonicalEncoder& encoder) const;",
    ):
        if marker not in header:
            errors.append(f"{CANONICAL_OVERLOAD_HEADER}: missing public overload marker {marker}")

    type_factory = function_body(
        implementation, "CanonicalCallableResult CanonicalCallableResult::type("
    )
    if type_factory is None:
        errors.append(
            f"{CANONICAL_OVERLOAD_HEADER_IMPLEMENTATION}: missing callable result type factory"
        )
    else:
        for marker in (
            "type.predefinedKind()",
            "if (kind == PredefinedTypeKind::Unit) { return unit(); }",
        ):
            if marker not in type_factory:
                errors.append(
                    f"{CANONICAL_OVERLOAD_HEADER_IMPLEMENTATION}: explicit Unit result must "
                    f"normalize to the unit variant via {marker}"
                )

    record_codecs = (
        (
            "void CanonicalCallableResult::encode(",
            (
                "encoder.encodeUint8(static_cast<uint8_t>(resultKind));",
                "impl->value.get<detail::CallableResultTypeData>().type.encode(encoder);",
            ),
            "callable result codec",
        ),
        (
            "void CanonicalGenericParameter::encode(",
            ("encoder.encodeSome();", "value.encode(encoder);"),
            "generic parameter codec",
        ),
        (
            "void CanonicalBoundObligation::encode(",
            ("impl->subject.encode(encoder);", "impl->bound.encode(encoder);"),
            "bound obligation codec",
        ),
        (
            "void CanonicalCallableParameter::encode(",
            (
                "impl->label.encode(encoder);",
                "impl->type.encode(encoder);",
                "encoder.encodeBool(impl->hasDefault);",
            ),
            "callable parameter codec",
        ),
    )
    for function_marker, field_markers, description in record_codecs:
        body = function_body(implementation, function_marker)
        if body is None:
            errors.append(
                f"{CANONICAL_OVERLOAD_HEADER_IMPLEMENTATION}: missing {description} body"
            )
        else:
            check_ordered_markers(
                body,
                field_markers,
                CANONICAL_OVERLOAD_HEADER_IMPLEMENTATION,
                description,
                errors,
            )

    if "encodeByteString" in implementation:
        errors.append(
            f"{CANONICAL_OVERLOAD_HEADER_IMPLEMENTATION}: canonical overload records must "
            "encode inline"
        )

    for marker in (
        "if (values.size() == 0) { return zc::none; }",
        "appendFlattenedUnion(zc::mv(value), flattened);",
        "values = sortUnique(zc::mv(flattened));",
        "obligations = sortUnique(zc::mv(obligations));",
        "if (receiver != zc::none || constructorResult) { return zc::none; }",
        "if (constructorResult || externalAbi != zc::none) { return zc::none; }",
        "} else if (receiver != zc::none || externalAbi != zc::none || !constructorResult) {",
    ):
        if marker not in implementation:
            errors.append(
                f"{CANONICAL_OVERLOAD_HEADER_IMPLEMENTATION}: missing overload admission marker "
                f"{marker}"
            )

    header_codec = function_body(implementation, "void OverloadHeader::encode(")
    if header_codec is None:
        errors.append(
            f"{CANONICAL_OVERLOAD_HEADER_IMPLEMENTATION}: missing canonical overload codec body"
        )
    else:
        check_ordered_markers(
            header_codec,
            (
                "encoder.encodeUint8(static_cast<uint8_t>(impl->callableKind));",
                "impl->name.encode(encoder);",
                "ZC_IF_SOME(value, impl->receiver)",
                "encodeSequence(encoder, impl->genericParameters.asPtr());",
                "encodeSequence(encoder, impl->obligations.asPtr());",
                "encodeSequence(encoder, impl->parameters.asPtr());",
                "impl->result.encode(encoder);",
                "ZC_IF_SOME(values, impl->raises)",
                "ZC_IF_SOME(value, impl->externalAbi)",
            ),
            CANONICAL_OVERLOAD_HEADER_IMPLEMENTATION,
            "canonical overload nine-field codec",
            errors,
        )

    for marker in (
        'ZC_TEST("Canonical overload leaf records and callable results pass exact vectors")',
        'ZC_TEST("Canonical overload header passes the complete nine-field vector clone and accessors")',
        'ZC_TEST("Canonical overload header rejects invalid tags and cross-kind admission")',
        'ZC_TEST("Canonical overload header rejects present-empty raises")',
        'ZC_TEST("Canonical overload header recursively normalizes raises unions")',
        'ZC_TEST("Canonical overload header sorts and deduplicates complete obligation bytes")',
        'expectHex(explicitUnit.encode().asPtr(), "01"_zc);',
        "ZC_EXPECT(explicitUnit.encode().asPtr() == unit.encode().asPtr());",
        "ZC_EXPECT(explicitUnit.kind() == CanonicalCallableResultKind::Unit);",
        "ZC_EXPECT(explicitUnit.type() == zc::none);",
        'expectHex(typed.encode().asPtr(), "030201"_zc);',
        '"0100000000000000016600000000000000000201020200000000000000000102010202000000000000000300000000000000017a02020000000000000000017a020200000000000000000161020101030201010000000000000002020102020101"_zc',
        "ZC_REQUIRE(header.parameters().size() == 3);",
        "ZC_REQUIRE(header.obligations().size() == 1);",
        "ZC_EXPECT(hasKind(header.obligations()[0].subject(), PredefinedTypeKind::I8));",
        "ZC_EXPECT(hasKind(header.obligations()[0].bound(), PredefinedTypeKind::I16));",
        'ZC_EXPECT(header.parameters()[0].label() == "z"_zc);',
        'ZC_EXPECT(header.parameters()[1].label() == "z"_zc);',
        'ZC_EXPECT(header.parameters()[2].label() == "a"_zc);',
    ):
        if marker not in test:
            errors.append(
                f"{CANONICAL_OVERLOAD_HEADER_TEST}: missing canonical overload test marker {marker}"
            )


def check_overload_header_digest_inventory(
    overrides: dict[Path, str], errors: list[str]
) -> None:
    header = read_text(OVERLOAD_HEADER_DIGEST, overrides)
    implementation = read_text(OVERLOAD_HEADER_DIGEST_IMPLEMENTATION, overrides)
    identity_cmake = read_text(IDENTITY_CMAKE, overrides)
    test = read_text(OVERLOAD_HEADER_DIGEST_TEST, overrides)

    source_marker = (
        f"${{CMAKE_CURRENT_SOURCE_DIR}}/{identity_source(OVERLOAD_HEADER_DIGEST_IMPLEMENTATION)}"
    )
    if source_marker not in identity_cmake:
        errors.append(
            f"{IDENTITY_CMAKE}: missing overload digest source "
            f"{identity_source(OVERLOAD_HEADER_DIGEST_IMPLEMENTATION)}"
        )

    for marker in (
        "class OverloadHeaderDigest final",
        "static OverloadHeaderDigest compute(const OverloadHeader& header);",
        "static zc::Maybe<OverloadHeaderDigest> fromBytes(",
        "OverloadHeaderDigest clone() const noexcept;",
        "void encode(CanonicalEncoder& encoder) const;",
        "class OverloadHeaderAuthority final",
        "static OverloadHeaderAuthority from(OverloadHeader&& header);",
        "OverloadHeaderAuthority clone() const;",
        "bool verify() const;",
        "bool sameRecordAs(const OverloadHeaderAuthority& other) const;",
    ):
        if marker not in header:
            errors.append(f"{OVERLOAD_HEADER_DIGEST}: missing overload digest marker {marker}")

    if 'constexpr auto kOverloadHeaderDomain = "zom.overload-header"_zc;' not in (
        implementation
    ):
        errors.append(
            f"{OVERLOAD_HEADER_DIGEST_IMPLEMENTATION}: invalid overload header digest domain"
        )

    compute_body = function_body(
        implementation, "OverloadHeaderDigest OverloadHeaderDigest::compute("
    )
    if compute_body is None:
        errors.append(
            f"{OVERLOAD_HEADER_DIGEST_IMPLEMENTATION}: missing overload header digest compute body"
        )
    else:
        check_ordered_markers(
            compute_body,
            (
                "const auto encodedHeader = header.encode();",
                "preimage.addAll(kOverloadHeaderDomain.asBytes());",
                "preimage.add(0x00);",
                "preimage.addAll(encodedHeader);",
                "sha256(preimage.asPtr())",
            ),
            OVERLOAD_HEADER_DIGEST_IMPLEMENTATION,
            "overload header digest preimage",
            errors,
        )

    if "Sha256Digest::fromBytes(bytes)" not in implementation:
        errors.append(
            f"{OVERLOAD_HEADER_DIGEST_IMPLEMENTATION}: digest admission must delegate exact "
            "length validation to Sha256Digest"
        )
    digest_codec = function_body(implementation, "void OverloadHeaderDigest::encode(")
    if digest_codec is None or "encoder.encodeDigest(digestValue);" not in digest_codec:
        errors.append(
            f"{OVERLOAD_HEADER_DIGEST_IMPLEMENTATION}: overload digest must encode as raw 32 bytes"
        )
    if "encodeByteString" in implementation:
        errors.append(
            f"{OVERLOAD_HEADER_DIGEST_IMPLEMENTATION}: overload digest must not encode a length "
            "wrapper"
        )

    authority_data = declaration_body(implementation, "struct OverloadHeaderAuthorityData final")
    expected_authority_data = (
        "OverloadHeaderDigest digest;",
        "OverloadHeader header;",
    )
    if authority_data is None or data_member_declarations(authority_data) != expected_authority_data:
        errors.append(
            f"{OVERLOAD_HEADER_DIGEST_IMPLEMENTATION}: overload authority must retain exactly "
            "digest and complete header"
        )
    verify_body = function_body(implementation, "bool OverloadHeaderAuthority::verify(")
    if verify_body is None or (
        "OverloadHeaderDigest::compute(impl->header) == impl->digest" not in verify_body
    ):
        errors.append(
            f"{OVERLOAD_HEADER_DIGEST_IMPLEMENTATION}: overload authority must verify its "
            "complete retained header"
        )
    comparison_body = function_body(
        implementation, "bool OverloadHeaderAuthority::sameRecordAs("
    )
    if comparison_body is None:
        errors.append(
            f"{OVERLOAD_HEADER_DIGEST_IMPLEMENTATION}: missing complete overload record comparison"
        )
    else:
        for marker in (
            "impl->header.encode();",
            "other.impl->header.encode();",
            "left.asPtr() == right.asPtr();",
        ):
            if marker not in comparison_body:
                errors.append(
                    f"{OVERLOAD_HEADER_DIGEST_IMPLEMENTATION}: complete overload record "
                    f"comparison is missing {marker}"
                )
        if "digest" in comparison_body:
            errors.append(
                f"{OVERLOAD_HEADER_DIGEST_IMPLEMENTATION}: complete overload record comparison "
                "must not use digest equality"
            )

    for marker in (
        'ZC_TEST("OverloadHeaderDigest passes the exact domain SHA and raw codec vector")',
        '"311a7707c91317c488448e3f407308246bc6ad8f627e73a019a9303a83ff1f2d"_zc',
        "ZC_EXPECT(value.encode().asPtr() == value.bytes());",
        'ZC_TEST("OverloadHeaderDigest admits exactly thirty-two verified bytes")',
        "OverloadHeaderDigest::fromBytes(zc::arrayPtr(bytes, 31))",
        "OverloadHeaderDigest::fromBytes(zc::arrayPtr(bytes, 32))",
        "OverloadHeaderDigest::fromBytes(zc::arrayPtr(bytes, 33))",
        'ZC_TEST("OverloadHeaderAuthority retains verifies clones and compares complete headers")',
        "ZC_EXPECT(authority.verify());",
        "ZC_EXPECT(authority.sameRecordAs(cloned));",
        "ZC_EXPECT(!authority.sameRecordAs(different));",
        "authority.header().encode().asPtr() != different.header().encode().asPtr()",
    ):
        if marker not in test:
            errors.append(
                f"{OVERLOAD_HEADER_DIGEST_TEST}: missing overload digest test marker {marker}"
            )


def check_canonical_impl_header_inventory(
    overrides: dict[Path, str], errors: list[str]
) -> None:
    header = read_text(CANONICAL_IMPL_HEADER, overrides)
    implementation = read_text(CANONICAL_IMPL_HEADER_IMPLEMENTATION, overrides)
    identity_cmake = read_text(IDENTITY_CMAKE, overrides)
    test = read_text(CANONICAL_IMPL_HEADER_TEST, overrides)

    source_marker = f"${{CMAKE_CURRENT_SOURCE_DIR}}/{identity_source(CANONICAL_IMPL_HEADER_IMPLEMENTATION)}"
    if source_marker not in identity_cmake:
        errors.append(
            f"{IDENTITY_CMAKE}: missing canonical impl source "
            f"{identity_source(CANONICAL_IMPL_HEADER_IMPLEMENTATION)}"
        )

    for enum_name, expected in (
        ("ImplPolarity", (("Positive", "0x01"), ("Negative", "0x02"))),
        ("ImplSafety", (("Safe", "0x01"), ("Unsafe", "0x02"))),
    ):
        if tagged_enum_members(header, enum_name) != expected:
            errors.append(
                f"{CANONICAL_IMPL_HEADER}: {enum_name} must retain its exact RFC 0018 tags"
            )

    for marker in (
        "class CanonicalTraitReference final",
        "static zc::Maybe<CanonicalTraitReference> from(",
        "CanonicalTraitReference clone() const;",
        "const CanonicalNameReference& name() const noexcept;",
        "zc::ArrayPtr<const CanonicalHeaderTypeSyntax> arguments() const noexcept;",
        "void encode(CanonicalEncoder& encoder) const;",
    ):
        if marker not in header:
            errors.append(f"{CANONICAL_IMPL_HEADER}: missing canonical trait marker {marker}")

    trait_data = declaration_body(implementation, "struct CanonicalTraitReferenceData final")
    expected_trait_data = (
        "CanonicalNameReference name;",
        "zc::Vector<CanonicalHeaderTypeSyntax> arguments;",
    )
    if trait_data is None or data_member_declarations(trait_data) != expected_trait_data:
        errors.append(
            f"{CANONICAL_IMPL_HEADER_IMPLEMENTATION}: canonical trait reference must contain "
            "exactly name and ordered arguments"
        )

    for forbidden in ("DefinitionKey", "SourceFileKey", "SourceSpan", "NodeId"):
        if re.search(rf"\b{forbidden}\b", header + implementation):
            errors.append(
                f"{CANONICAL_IMPL_HEADER}: canonical trait reference must not contain {forbidden}"
            )
    if "encodeByteString" in implementation:
        errors.append(
            f"{CANONICAL_IMPL_HEADER_IMPLEMENTATION}: canonical trait fields must encode inline"
        )

    factory_body = function_body(
        implementation, "zc::Maybe<CanonicalTraitReference> CanonicalTraitReference::from("
    )
    trait_root_rejection = re.compile(
        r"if\s*\(\s*rootKind\s*!=\s*CanonicalNameRootKind::Absolute\s*&&\s*"
        r"rootKind\s*!=\s*CanonicalNameRootKind::Relative\s*\)\s*\{\s*"
        r"return\s+zc::none;\s*\}"
    )
    if factory_body is None or trait_root_rejection.search(factory_body) is None:
        errors.append(
            f"{CANONICAL_IMPL_HEADER_IMPLEMENTATION}: canonical trait root must admit only "
            "absolute and relative names"
        )

    trait_codec = function_body(implementation, "void CanonicalTraitReference::encode(")
    if trait_codec is None:
        errors.append(
            f"{CANONICAL_IMPL_HEADER_IMPLEMENTATION}: missing canonical trait codec body"
        )
    else:
        check_ordered_markers(
            trait_codec,
            (
                "impl->name.encode(encoder);",
                "encoder.encodeSequenceSize(impl->arguments.size());",
                "argument.encode(encoder);",
            ),
            CANONICAL_IMPL_HEADER_IMPLEMENTATION,
            "canonical trait field codec",
            errors,
        )

    for marker in (
        "class ImplHeader final",
        "static zc::Maybe<ImplHeader> from(",
        "ImplHeader clone() const;",
        "zc::ArrayPtr<const CanonicalGenericParameter> genericParameters() const noexcept;",
        "const CanonicalTraitReference& trait() const noexcept;",
        "const CanonicalHeaderTypeSyntax& selfType() const noexcept;",
        "zc::ArrayPtr<const CanonicalBoundObligation> obligations() const noexcept;",
        "void encode(CanonicalEncoder& encoder) const;",
    ):
        if marker not in header:
            errors.append(f"{CANONICAL_IMPL_HEADER}: missing canonical impl header marker {marker}")

    impl_data = declaration_body(implementation, "struct CanonicalImplHeaderData final")
    expected_impl_data = (
        "zc::Vector<CanonicalGenericParameter> genericParameters;",
        "ImplPolarity polarity;",
        "ImplSafety safety;",
        "CanonicalTraitReference trait;",
        "CanonicalHeaderTypeSyntax selfType;",
        "zc::Vector<CanonicalBoundObligation> obligations;",
    )
    if impl_data is None or data_member_declarations(impl_data) != expected_impl_data:
        errors.append(
            f"{CANONICAL_IMPL_HEADER_IMPLEMENTATION}: canonical impl header must contain exactly "
            "generics, polarity, safety, trait, self type, and obligations"
        )

    impl_factory = function_body(
        implementation, "zc::Maybe<ImplHeader> ImplHeader::from("
    )
    if impl_factory is None:
        errors.append(f"{CANONICAL_IMPL_HEADER_IMPLEMENTATION}: missing impl header admission")
    else:
        for marker in (
            "!isCanonicalImplHeaderValue(polarity)",
            "!isCanonicalImplHeaderValue(safety)",
            "obligations = sortUnique(zc::mv(obligations));",
        ):
            if marker not in impl_factory:
                errors.append(
                    f"{CANONICAL_IMPL_HEADER_IMPLEMENTATION}: impl header admission is missing {marker}"
                )

    impl_codec = function_body(implementation, "void ImplHeader::encode(")
    if impl_codec is None:
        errors.append(f"{CANONICAL_IMPL_HEADER_IMPLEMENTATION}: missing impl header codec")
    else:
        check_ordered_markers(
            impl_codec,
            (
                "encodeSequence(encoder, impl->genericParameters.asPtr());",
                "encoder.encodeUint8(static_cast<uint8_t>(impl->polarity));",
                "encoder.encodeUint8(static_cast<uint8_t>(impl->safety));",
                "impl->trait.encode(encoder);",
                "impl->selfType.encode(encoder);",
                "encodeSequence(encoder, impl->obligations.asPtr());",
            ),
            CANONICAL_IMPL_HEADER_IMPLEMENTATION,
            "canonical impl header field codec",
            errors,
        )

    for marker in (
        'ZC_TEST("Canonical impl polarity and safety retain exact closed tags")',
        "static_cast<uint8_t>(ImplPolarity::Positive) == 0x01",
        "static_cast<uint8_t>(ImplPolarity::Negative) == 0x02",
        "static_cast<uint8_t>(ImplSafety::Safe) == 0x01",
        "static_cast<uint8_t>(ImplSafety::Unsafe) == 0x02",
        'ZC_TEST("CanonicalTraitReference admits absolute and relative roots but rejects generic roots")',
        "ZC_EXPECT(generic == zc::none);",
        'ZC_TEST("CanonicalTraitReference passes the exact fieldwise vector and preserves arguments")',
        '"0100000000000000020000000000000003706b67000000000000000554726169740000000000000003020202010202"_zc',
        "ZC_REQUIRE(value.arguments().size() == 3);",
        "value.arguments()[0].predefinedKind() == PredefinedTypeKind::I16",
        "value.arguments()[1].predefinedKind() == PredefinedTypeKind::I8",
        "value.arguments()[2].predefinedKind() == PredefinedTypeKind::I16",
        'ZC_TEST("ImplHeader passes the exact RFC 0018 fieldwise vector")',
        '"0000000000000000010102000000000000000100000000000000055472616974"',
        'ZC_TEST("ImplHeader retains generic order and sorts unique obligations")',
        "ZC_REQUIRE(value.genericParameters().size() == 2);",
        "ZC_REQUIRE(value.obligations().size() == 2);",
        'ZC_TEST("ImplHeader rejects values outside the closed tag domains")',
        "static_cast<ImplPolarity>(0xff)",
        "static_cast<ImplSafety>(0xff)",
    ):
        if marker not in test:
            errors.append(
                f"{CANONICAL_IMPL_HEADER_TEST}: missing canonical trait test marker {marker}"
            )


def check_module_resolution_key_architecture(
    overrides: dict[Path, str], errors: list[str]
) -> None:
    header = read_text(MODULE_RESOLUTION_KEY, overrides)
    implementation = read_text(MODULE_RESOLUTION_KEY_IMPLEMENTATION, overrides)
    identity_cmake = read_text(IDENTITY_CMAKE, overrides)
    test = read_text(MODULE_RESOLUTION_KEY_TEST, overrides)
    test_cmake = read_text(IDENTITY_TEST_CMAKE, overrides)

    policy_enums = (
        ("UnicodeNormalizationPolicy", (("Nfc", "0x01"),)),
        ("CaseComparisonPolicy", (("CaseSensitive", "0x01"),)),
        ("SymlinkHandlingPolicy", (("ResolveThenConfine", "0x01"),)),
        ("ModuleContainmentPolicy", (("DeclaredRootsOnly", "0x01"),)),
        (
            "LocalModuleLookupPolicy",
            (("RequesterAncestryAndCrateRoot", "0x01"),),
        ),
        ("DependencyAliasLookupPolicy", (("ExactFirstSegment", "0x01"),)),
        ("PreludeLookupPolicy", (("ConfiguredCratePrelude", "0x01"),)),
        (
            "ModuleCandidateSelectionPolicy",
            (("AllDistinctMatchesNoPrecedence", "0x01"),),
        ),
    )
    for enum_name, expected_members in policy_enums:
        if tagged_enum_members(header, enum_name) != expected_members:
            errors.append(
                f"{MODULE_RESOLUTION_KEY}: {enum_name} must retain its exact RFC 0018 tag"
            )

    dependency_members = (
        ("Import", "0x01"),
        ("ForeignReexport", "0x02"),
        ("ModuleAlias", "0x03"),
        ("Prelude", "0x04"),
    )
    if tagged_enum_members(header, "ModuleDependencyKind") != dependency_members:
        errors.append(
            f"{MODULE_RESOLUTION_KEY}: ModuleDependencyKind must retain its four exact tags"
        )

    dependency_kind_pattern = re.compile(r"\benum\s+class\s+ModuleDependencyKind\b")
    dependency_kind_owners = matching_occurrence_paths(dependency_kind_pattern, overrides)
    if dependency_kind_owners != (MODULE_RESOLUTION_KEY,):
        owners = ", ".join(str(path) for path in dependency_kind_owners) or "none"
        errors.append(
            f"{MODULE_RESOLUTION_KEY}: ModuleDependencyKind must have one identity-layer owner; "
            f"found {owners}"
        )

    bucket = declaration_body(header, "class ModuleCatalogPathBucketKey final")
    expected_bucket_fields = (
        "CrateKey crateValue;",
        "zc::Vector<ModulePathSegment> pathValue;",
    )
    if bucket is None:
        errors.append(f"{MODULE_RESOLUTION_KEY}: missing ModuleCatalogPathBucketKey declaration")
    elif data_member_declarations(bucket) != expected_bucket_fields:
        errors.append(
            f"{MODULE_RESOLUTION_KEY}: ModuleCatalogPathBucketKey must contain exactly crate and path"
        )

    bucket_from = function_body(implementation, "ModuleCatalogPathBucketKey::from(")
    if bucket_from is None or "if (path.size() == 0) { return zc::none; }" not in bucket_from:
        errors.append(
            f"{MODULE_RESOLUTION_KEY_IMPLEMENTATION}: catalog bucket must reject an empty path"
        )
    bucket_encode = function_body(implementation, "ModuleCatalogPathBucketKey::encode() const")
    if bucket_encode is None:
        errors.append(f"{MODULE_RESOLUTION_KEY_IMPLEMENTATION}: missing catalog bucket codec")
    else:
        check_ordered_markers(
            bucket_encode,
            (
                "crateValue.encode(record);",
                "record.encodeSequenceSize(pathValue.size());",
                "for (const auto& segment : pathValue)",
            ),
            MODULE_RESOLUTION_KEY_IMPLEMENTATION,
            "catalog bucket codec",
            errors,
        )
        if 'constexpr auto domain = "zom.module-catalog-path-bucket"_zc;' not in bucket_encode:
            errors.append(
                f"{MODULE_RESOLUTION_KEY_IMPLEMENTATION}: invalid catalog bucket domain"
            )
    bucket_decode = function_body(
        implementation, "ModuleCatalogPathBucketKey::decodeCanonical("
    )
    if bucket_decode is None or any(
        marker not in bucket_decode
        for marker in (
            "CrateKey::decodeCanonical(decoder)",
            "decoder.decodeSequenceSize(kMaximumModulePathSegments)",
            "!decoder.finished()",
            "value.encode().asPtr() != bytes",
        )
    ):
        errors.append(
            f"{MODULE_RESOLUTION_KEY_IMPLEMENTATION}: catalog bucket decoder must be exact bounded and canonical"
        )

    ancestry = declaration_body(header, "class RequesterModuleAncestry final")
    expected_ancestry_fields = (
        "ModuleKey requesterValue;",
        "zc::Vector<ModuleKey> ancestryValue;",
    )
    if ancestry is None:
        errors.append(f"{MODULE_RESOLUTION_KEY}: missing RequesterModuleAncestry declaration")
    elif data_member_declarations(ancestry) != expected_ancestry_fields:
        errors.append(
            f"{MODULE_RESOLUTION_KEY}: RequesterModuleAncestry must contain exactly requester and ancestry"
        )

    ancestry_from = function_body(implementation, "RequesterModuleAncestry::from(")
    if ancestry_from is None:
        errors.append(
            f"{MODULE_RESOLUTION_KEY_IMPLEMENTATION}: missing requester ancestry admission"
        )
    else:
        for marker, description in (
            (
                "ancestry.size() == 0 || !sameKey(requester, ancestry[0])",
                "reject an empty or requester-mismatched chain",
            ),
            (
                "child.path().size() != parent.path().size() + 1",
                "require strict lexical parents",
            ),
            (
                "child.path()[segment] != parent.path()[segment]",
                "require exact lexical-parent prefixes",
            ),
        ):
            if marker not in ancestry_from:
                errors.append(
                    f"{MODULE_RESOLUTION_KEY_IMPLEMENTATION}: requester ancestry must {description}"
                )

    ancestry_encode = function_body(implementation, "RequesterModuleAncestry::encode() const")
    if ancestry_encode is None:
        errors.append(f"{MODULE_RESOLUTION_KEY_IMPLEMENTATION}: missing requester ancestry codec")
    else:
        check_ordered_markers(
            ancestry_encode,
            (
                "encoder.encodeSequenceSize(ancestryValue.size());",
                "for (const auto& module : ancestryValue)",
                "module.encode(encoder);",
            ),
            MODULE_RESOLUTION_KEY_IMPLEMENTATION,
            "requester ancestry codec",
            errors,
        )
        if "requesterValue.encode" in ancestry_encode:
            errors.append(
                f"{MODULE_RESOLUTION_KEY_IMPLEMENTATION}: requester ancestry value codec must not repeat the query key"
            )
    ancestry_decode = function_body(
        implementation, "RequesterModuleAncestry::decodeCanonical("
    )
    if ancestry_decode is None or any(
        marker not in ancestry_decode
        for marker in (
            "decoder.decodeSequenceSize(kMaximumModulePathSegments)",
            "ModuleKey::decodeCanonical(decoder)",
            "!decoder.finished()",
            "from(zc::mv(requester), zc::mv(ancestry))",
        )
    ):
        errors.append(
            f"{MODULE_RESOLUTION_KEY_IMPLEMENTATION}: requester ancestry decoder must be exact bounded and structural"
        )

    bucket_value = declaration_body(header, "class ModuleCatalogPathBucket final")
    expected_bucket_value_fields = (
        "ModuleCatalogPathBucketKey keyValue;",
        "zc::Maybe<ModuleKey> moduleValue;",
    )
    if bucket_value is None:
        errors.append(f"{MODULE_RESOLUTION_KEY}: missing ModuleCatalogPathBucket declaration")
    elif data_member_declarations(bucket_value) != expected_bucket_value_fields:
        errors.append(
            f"{MODULE_RESOLUTION_KEY}: ModuleCatalogPathBucket must contain exactly key and optional module"
        )

    bucket_present = function_body(implementation, "ModuleCatalogPathBucket::present(")
    if bucket_present is None or (
        "!sameCrate(key.crate(), module.crate()) || !samePath(key.path(), module.path())"
        not in bucket_present
    ):
        errors.append(
            f"{MODULE_RESOLUTION_KEY_IMPLEMENTATION}: present catalog bucket must require exact crate and path equality"
        )

    bucket_value_encode = function_body(implementation, "ModuleCatalogPathBucket::encode() const")
    if bucket_value_encode is None:
        errors.append(f"{MODULE_RESOLUTION_KEY_IMPLEMENTATION}: missing catalog bucket value codec")
    else:
        for marker in ("encoder.encodeSome();", "module.encode(encoder);", "encoder.encodeNone();"):
            if marker not in bucket_value_encode:
                errors.append(
                    f"{MODULE_RESOLUTION_KEY_IMPLEMENTATION}: catalog bucket value codec is missing {marker}"
                )
        if "keyValue.encode" in bucket_value_encode:
            errors.append(
                f"{MODULE_RESOLUTION_KEY_IMPLEMENTATION}: catalog bucket value codec must not repeat the query key"
            )
    bucket_value_decode = function_body(
        implementation, "ModuleCatalogPathBucket::decodeCanonical("
    )
    if bucket_value_decode is None or any(
        marker not in bucket_value_decode
        for marker in (
            "decoder.decodeBool()",
            "ModuleKey::decodeCanonical(decoder)",
            "!decoder.finished()",
            "present(zc::mv(key), zc::mv(value))",
        )
    ):
        errors.append(
            f"{MODULE_RESOLUTION_KEY_IMPLEMENTATION}: catalog bucket value decoder must validate exact optional membership"
        )

    policy = declaration_body(header, "class ModuleResolutionPolicyKey final")
    expected_policy_fields = (
        "UnicodeNormalizationPolicy unicodeNormalizationValue;",
        "CaseComparisonPolicy caseComparisonValue;",
        "SymlinkHandlingPolicy symlinkHandlingValue;",
        "ModuleContainmentPolicy containmentValue;",
        "LocalModuleLookupPolicy localLookupValue;",
        "DependencyAliasLookupPolicy dependencyAliasLookupValue;",
        "PreludeLookupPolicy preludeLookupValue;",
        "ModuleCandidateSelectionPolicy candidateSelectionValue;",
    )
    if policy is None:
        errors.append(f"{MODULE_RESOLUTION_KEY}: missing ModuleResolutionPolicyKey declaration")
    elif data_member_declarations(policy) != expected_policy_fields:
        errors.append(
            f"{MODULE_RESOLUTION_KEY}: ModuleResolutionPolicyKey must contain exactly eight policy fields"
        )

    policy_encode = function_body(implementation, "ModuleResolutionPolicyKey::encode() const")
    if policy_encode is None:
        errors.append(f"{MODULE_RESOLUTION_KEY_IMPLEMENTATION}: missing resolution policy codec")
    else:
        check_ordered_markers(
            policy_encode,
            tuple(
                f"record.add(static_cast<uint8_t>({field.split()[-1][:-1]}));"
                for field in expected_policy_fields
            ),
            MODULE_RESOLUTION_KEY_IMPLEMENTATION,
            "resolution policy codec",
            errors,
        )
        if "zc::Vector<uint8_t> record(8);" not in policy_encode:
            errors.append(
                f"{MODULE_RESOLUTION_KEY_IMPLEMENTATION}: resolution policy codec must emit eight fields"
            )
        if 'constexpr auto domain = "zom.module-resolution-policy"_zc;' not in policy_encode:
            errors.append(
                f"{MODULE_RESOLUTION_KEY_IMPLEMENTATION}: invalid resolution policy domain"
            )
    policy_decode = function_body(
        implementation, "ModuleResolutionPolicyKey::decodeCanonical("
    )
    if policy_decode is None or any(
        marker not in policy_decode
        for marker in (
            'constexpr auto domain = "zom.module-resolution-policy"_zc;',
            "decoder.decodeUint8()",
            "!decoder.finished()",
            "static_cast<ModuleCandidateSelectionPolicy>",
        )
    ):
        errors.append(
            f"{MODULE_RESOLUTION_KEY_IMPLEMENTATION}: resolution policy decoder must close all eight fields"
        )

    request = declaration_body(header, "class ModuleResolutionKey final")
    expected_request_fields = (
        "ModuleKey requesterValue;",
        "ModuleDependencyKind kindValue;",
        "zc::Maybe<zc::Vector<ModulePathSegment>> normalizedPathValue;",
        "zc::Maybe<DependencyAlias> dependencyAliasValue;",
        "ModuleResolutionPolicyKey policyValue;",
    )
    if request is None:
        errors.append(f"{MODULE_RESOLUTION_KEY}: missing ModuleResolutionKey declaration")
    else:
        request_fields = data_member_declarations(request)
        request_field_surface = "\n".join(request_fields)
        if request_fields != expected_request_fields:
            errors.append(
                f"{MODULE_RESOLUTION_KEY}: ModuleResolutionKey must contain exactly five semantic fields"
            )
        if re.search(r"\b(?:SourceSpan|SourceFileKey|NodeId)\b", request_field_surface):
            errors.append(
                f"{MODULE_RESOLUTION_KEY}: ModuleResolutionKey provenance fields are forbidden"
            )
        if re.search(
            r"\b(?:requestedTarget|resolvedTarget|targetModule|targetValue)\w*\b",
            request_field_surface,
        ):
            errors.append(
                f"{MODULE_RESOLUTION_KEY}: ModuleResolutionKey requested target fields are forbidden"
            )
        if re.search(
            r"\b\w*(?:Environment|environment|Revision|revision)\w*\b",
            request_field_surface,
        ):
            errors.append(
                f"{MODULE_RESOLUTION_KEY}: ModuleResolutionKey environment or revision fields are forbidden"
            )

    request_encode = function_body(implementation, "ModuleResolutionKey::encode() const")
    if request_encode is None:
        errors.append(f"{MODULE_RESOLUTION_KEY_IMPLEMENTATION}: missing resolution request codec")
    else:
        check_ordered_markers(
            request_encode,
            (
                "requesterValue.encode(record);",
                "record.encodeUint8(static_cast<uint8_t>(kindValue));",
                "ZC_IF_SOME(path, normalizedPathValue)",
                "ZC_IF_SOME(alias, dependencyAliasValue)",
                "const auto policyBytes = policyValue.encode();",
            ),
            MODULE_RESOLUTION_KEY_IMPLEMENTATION,
            "resolution request codec",
            errors,
        )
        if 'constexpr auto domain = "zom.module-resolution"_zc;' not in request_encode:
            errors.append(
                f"{MODULE_RESOLUTION_KEY_IMPLEMENTATION}: invalid resolution request domain"
            )
    request_decode = function_body(implementation, "ModuleResolutionKey::decodeCanonical(")
    if request_decode is None or any(
        marker not in request_decode
        for marker in (
            'constexpr auto domain = "zom.module-resolution"_zc;',
            "ModuleKey::decodeCanonical(decoder)",
            "decoder.decodeBool()",
            "DependencyAlias::decodeCanonical(decoder)",
            "ModuleResolutionPolicyKey::decodeCanonical(value)",
            "!decoder.finished()",
        )
    ):
        errors.append(
            f"{MODULE_RESOLUTION_KEY_IMPLEMENTATION}: resolution request decoder must be exact bounded and compositional"
        )

    if "${CMAKE_CURRENT_SOURCE_DIR}/key/module-resolution-key.cc" not in identity_cmake:
        errors.append(f"{IDENTITY_CMAKE}: missing module-resolution-key.cc registration")
    for marker in (
        '#include "zomlang/compiler/identity/key/module-resolution-key.h"',
        'ZC_TEST("ModuleCatalogPathBucketKey passes the fixed canonical codec vector")',
        'ZC_TEST("ModuleCatalogPathBucketKey rejects an empty canonical path")',
        'ZC_TEST("ModuleCatalogPathBucketKey decoder is exact bounded and domain separated")',
        'ZC_TEST("RequesterModuleAncestry admits the exact requester-first lexical chain")',
        'ZC_TEST("RequesterModuleAncestry rejects empty mismatched and skipped chains")',
        'ZC_TEST("RequesterModuleAncestry decoder rejects truncation trailing data and invalid chains")',
        'ZC_TEST("ModuleCatalogPathBucket admits exact present and absent values")',
        'ZC_TEST("ModuleCatalogPathBucket decoder validates its external key")',
        'ZC_TEST("ModuleResolutionPolicyKey passes the exact fixed byte vector")',
        'ZC_TEST("ModuleResolutionPolicyKey decoder closes the domain and record")',
        'ZC_TEST("ModuleResolutionKey passes fixed import and prelude vectors")',
        'ZC_TEST("ModuleResolutionKey encodes all dependency-kind tags")',
        'ZC_TEST("ModuleResolutionKey rejects malformed enums and records")',
        'ZC_TEST("ModuleResolutionKey decoder is exact and rejects closed-tag mutations")',
    ):
        if marker not in test:
            errors.append(f"{MODULE_RESOLUTION_KEY_TEST}: missing RFC 0018 test marker {marker}")
    if not re.search(
        r"add_ztest_unit_tests_from_directory\s*\(\s*\$\{CMAKE_CURRENT_SOURCE_DIR\}"
        r"\s+LIBRARIES\s+frontend\s*\)",
        test_cmake,
        re.DOTALL,
    ):
        errors.append(f"{IDENTITY_TEST_CMAKE}: missing identity unit-test discovery registration")

    module_resolution_implementation = read_text(MODULE_RESOLUTION_IMPLEMENTATION, overrides)
    obsolete_ancestry_owners = matching_files(
        re.compile(r"\bModuleRequesterAncestry\b"), overrides
    )
    for path in sorted(obsolete_ancestry_owners):
        errors.append(f"{path}: obsolete ModuleRequesterAncestry identity surface is forbidden")

    resolver_impl = declaration_body(
        module_resolution_implementation, "struct StructuralModuleResolver::Impl final"
    )
    for marker in (
        "zc::Vector<identity::RequesterModuleAncestry> requesterAncestry;",
        "zc::Vector<identity::ModuleCatalogPathBucket> catalogBuckets;",
        "zc::TreeMap<zc::String, size_t> bucketSlots;",
    ):
        if resolver_impl is None or marker not in resolver_impl:
            errors.append(
                f"{MODULE_RESOLUTION_IMPLEMENTATION}: resolver must retain admitted exact input {marker}"
            )

    module_resolution_header = read_text(MODULE_RESOLUTION, overrides)
    if "StructuralModuleResolver::resolve(" in module_resolution_implementation or re.search(
        r"\bresolve\s*\(\s*ModuleDependencyRequest&&", module_resolution_header
    ):
        errors.append(
            f"{MODULE_RESOLUTION}: batch structural resolution authority is forbidden"
        )


def check_semantic_import_binding_key_architecture(
    overrides: dict[Path, str], errors: list[str]
) -> None:
    header = read_text(SEMANTIC_IMPORT_BINDING_KEY, overrides)
    implementation = read_text(SEMANTIC_IMPORT_BINDING_KEY_IMPLEMENTATION, overrides)
    identity_cmake = read_text(IDENTITY_CMAKE, overrides)
    test = read_text(SEMANTIC_IMPORT_BINDING_KEY_TEST, overrides)

    operation_tags = tagged_enum_members(header, "SemanticImportOperation")
    expected_operation_tags = (
        ("Import", "0x01"),
        ("ForeignReexport", "0x02"),
        ("ModuleAlias", "0x03"),
    )
    if operation_tags != expected_operation_tags:
        errors.append(
            f"{SEMANTIC_IMPORT_BINDING_KEY}: SemanticImportOperation must retain its three exact semantic tags"
        )

    key = declaration_body(header, "class ImportBindingKey final")
    expected_fields = (
        "ModuleKey requesterValue;",
        "ModuleResolutionKey resolutionValue;",
        "SemanticImportOperation operationValue;",
        "DefinitionNamespace sourceNamespaceValue;",
        "DeclaredDefinitionName sourceNameValue;",
        "DefinitionNamespace localNamespaceValue;",
        "DeclaredDefinitionName localNameValue;",
    )
    if key is None:
        errors.append(f"{SEMANTIC_IMPORT_BINDING_KEY}: missing ImportBindingKey declaration")
    else:
        fields = data_member_declarations(key)
        if fields != expected_fields:
            errors.append(
                f"{SEMANTIC_IMPORT_BINDING_KEY}: ImportBindingKey must contain exactly seven semantic fields"
            )
        field_surface = "\n".join(fields)
        if re.search(
            r"\b(?:SourceSpan|SourceFileKey|NodeId|DefId)\b|"
            r"\b\w*(?:Revision|revision|Site|site|Span|span)\w*\b",
            field_surface,
        ):
            errors.append(
                f"{SEMANTIC_IMPORT_BINDING_KEY}: provenance handles and revisions are forbidden"
            )

    admission = function_body(implementation, "ImportBindingKey::from(")
    if admission is None:
        errors.append(
            f"{SEMANTIC_IMPORT_BINDING_KEY_IMPLEMENTATION}: missing semantic import key admission"
        )
    else:
        for marker in (
            "!sameModule(requester, resolution.requester())",
            "!operationMatchesResolution(operation, resolution.dependencyKind())",
            "!isValid(sourceNamespace)",
            "!isValid(localNamespace)",
        ):
            if marker not in admission:
                errors.append(
                    f"{SEMANTIC_IMPORT_BINDING_KEY_IMPLEMENTATION}: semantic import key admission is missing {marker}"
                )

    key_encode = function_body(implementation, "ImportBindingKey::encode() const")
    if key_encode is None:
        errors.append(
            f"{SEMANTIC_IMPORT_BINDING_KEY_IMPLEMENTATION}: missing semantic import key codec"
        )
    else:
        check_ordered_markers(
            key_encode,
            (
                "requesterValue.encode(record);",
                "const auto resolutionBytes = resolutionValue.encode();",
                "record.encodeByteString(resolutionBytes.asPtr());",
                "record.encodeUint8(static_cast<uint8_t>(operationValue));",
                "record.encodeUint8(static_cast<uint8_t>(sourceNamespaceValue));",
                "sourceNameValue.encode(record);",
                "record.encodeUint8(static_cast<uint8_t>(localNamespaceValue));",
                "localNameValue.encode(record);",
            ),
            SEMANTIC_IMPORT_BINDING_KEY_IMPLEMENTATION,
            "semantic import key codec",
            errors,
        )
        if 'constexpr auto domain = "zom.semantic-import-binding"_zc;' not in key_encode:
            errors.append(
                f"{SEMANTIC_IMPORT_BINDING_KEY_IMPLEMENTATION}: invalid semantic import key domain"
            )

    if "${CMAKE_CURRENT_SOURCE_DIR}/key/import-binding-key.cc" not in identity_cmake:
        errors.append(f"{IDENTITY_CMAKE}: missing semantic-import-binding-key.cc registration")
    for marker in (
        '#include "zomlang/compiler/identity/key/import-binding-key.h"',
        'ZC_TEST("ImportBindingKey passes the fixed canonical codec vector")',
        'ZC_TEST("ImportBindingKey distinguishes every semantic field")',
        'ZC_TEST("ImportBindingKey rejects requester operation and namespace mismatches")',
        'ZC_TEST("ImportBindingKey canonicalizes source and local names to NFC")',
    ):
        if marker not in test:
            errors.append(
                f"{SEMANTIC_IMPORT_BINDING_KEY_TEST}: missing RFC 0017 test marker {marker}"
            )


def check_stable_identity_architecture(
    overrides: dict[Path, str], errors: list[str]
) -> None:
    definition_key = read_text(DEFINITION_KEY, overrides)
    definition_key_implementation = read_text(DEFINITION_KEY_IMPLEMENTATION, overrides)
    interner = read_text(CANONICAL_IDENTITY_INTERNER, overrides)
    interner_implementation = read_text(
        CANONICAL_IDENTITY_INTERNER_IMPLEMENTATION, overrides
    )
    build_script_key = read_text(BUILD_SCRIPT_KEY, overrides)
    build_script_key_implementation = read_text(BUILD_SCRIPT_KEY_IMPLEMENTATION, overrides)
    crate_key = read_text(CRATE_KEY, overrides)
    source_key = read_text(SOURCE_KEY, overrides)
    source_key_implementation = read_text(SOURCE_KEY_IMPLEMENTATION, overrides)
    module_resolution = read_text(MODULE_RESOLUTION, overrides)
    package_request = read_text(PACKAGE_COMPILATION_REQUEST, overrides)
    package_request_implementation = read_text(
        PACKAGE_COMPILATION_REQUEST_IMPLEMENTATION, overrides
    )
    crate_graph = read_text(CRATE_GRAPH, overrides)
    crate_graph_implementation = read_text(CRATE_GRAPH_IMPLEMENTATION, overrides)
    session = read_text(COMPILER_SESSION, overrides)

    for marker in (
        "class DefinitionIdentityRecord final",
        "class ImplIdentityRecord final",
        "class DefinitionIdentityAuthority final",
        "class ImplIdentityAuthority final",
        "class EnclosingStableOwnerKey final",
        "class GenericParameterKey final",
        "class CallableParameterKey final",
        "Sha256Digest digestValue;",
    ):
        if marker not in definition_key:
            errors.append(f"{DEFINITION_KEY}: missing RFC 0018 identity marker {marker}")
    for marker in (
        'constexpr auto kDefinitionDomain = "zom.named-item-header"_zc;',
        'constexpr auto kImplDomain = "zom.impl-header"_zc;',
        'constexpr auto kGenericParameterDomain = "zom.generic-parameter"_zc;',
        'constexpr auto kCallableParameterDomain = "zom.callable-parameter"_zc;',
        "encodeSequence(encoder, impl->owners.asPtr());",
        "impl->header.encode(encoder);",
    ):
        if marker not in definition_key_implementation:
            errors.append(
                f"{DEFINITION_KEY_IMPLEMENTATION}: missing RFC 0018 codec marker {marker}"
            )
    for forbidden in (
        "DefinitionNameKey",
        "AnonymousDefinitionRole",
        "DefinitionPathSegment",
        "ImplPathSegment",
        "DefinitionPathComponent",
        "siblingOrdinal",
    ):
        if forbidden in definition_key:
            errors.append(
                f"{DEFINITION_KEY}: position-derived identity surface {forbidden} is forbidden"
            )
    definition_key_class = declaration_body(definition_key, "class DefinitionKey final")
    impl_key_class = declaration_body(definition_key, "class ImplKey final")
    for name, body in (("DefinitionKey", definition_key_class), ("ImplKey", impl_key_class)):
        if body is None:
            continue
        for forbidden in ("ModuleKey", "SourceFileKey", "SourceSpan", " path(", " module("):
            if forbidden in body:
                errors.append(
                    f"{DEFINITION_KEY}: {name} must be a raw branded digest without {forbidden.strip()}"
                )

    for marker in (
        "enum class IdentityInternerFailure : uint8_t",
        "AllocationFailure = 0x01",
        "SlotOverflow = 0x02",
        "ForeignBrand = 0x03",
        "MalformedRecord = 0x04",
        "CanonicalCollision = 0x05",
        "using CompilationUnitIdentityEntry =",
        "using CrateIdentityEntry =",
        "using SourceFileIdentityEntry =",
        "using ModuleIdentityEntry =",
        "using DefinitionIdentityEntry =",
        "using ImplementationIdentityEntry =",
        "using GenericParameterIdentityEntry =",
        "using CallableParameterIdentityEntry =",
        "class IdentityInternerSet final",
        "static zc::Maybe<IdentityInternerSet> create(",
        "const CompilationUnitIdentity& key) const;",
        "const CrateKey& key) const;",
        "const SourceFileKey& key) const;",
        "const ModuleKey& key) const;",
        "const DefinitionKey& key) const;",
        "const ImplKey& key) const;",
        "const GenericParameterKey& key) const;",
        "const CallableParameterKey& key) const;",
    ):
        if marker not in interner:
            errors.append(
                f"{CANONICAL_IDENTITY_INTERNER}: missing arena interner marker {marker}"
            )
    if "frozen-registry.h" in interner:
        errors.append(
            f"{CANONICAL_IDENTITY_INTERNER}: append-only interner must not depend on frozen registries"
        )
    for marker in (
        "zc::Arena arena;",
        "zc::MutexGuarded<Data> data;",
        "factory.claimCanonicalIdentityInternerSet(context)",
        "IdentityInternerFailure::CanonicalCollision",
        "IdentityInternerFailure::SlotOverflow",
        "DefinitionKey::compute(record) == key",
        "ImplKey::compute(record) == key",
        "GenericParameterKey::compute(record) == key",
        "CallableParameterKey::compute(record) == key",
        "compilationUnits;",
        "crates;",
        "sourceFiles;",
        "modules;",
        "definitions;",
        "implementations;",
        "genericParameters;",
        "callableParameters;",
    ):
        if marker not in interner_implementation:
            errors.append(
                f"{CANONICAL_IDENTITY_INTERNER_IMPLEMENTATION}: missing arena interner implementation marker {marker}"
            )
    if "${CMAKE_CURRENT_SOURCE_DIR}/canonical/identity-interner-set.cc" not in read_text(
        IDENTITY_CMAKE, overrides
    ):
        errors.append(
            f"{IDENTITY_CMAKE}: missing canonical-identity-interner-set.cc registration"
        )
    for marker in (
        "zc::Maybe<identity::IdentityInternerSet> identityInternerSet;",
        "identity::IdentityInternerSet::create(contextFactory, resources->contextBrand)",
    ):
        if marker not in session:
            errors.append(
                f"{COMPILER_SESSION}: missing arena-owned identity interner marker {marker}"
            )

    output_identity_files = matching_files(re.compile(r"\bBuildScriptOutputKey\b"), overrides)
    for path in sorted(output_identity_files):
        errors.append(f"{path}: output-derived BuildScriptOutputKey identity is forbidden")

    required_producer_markers = (
        "class BuildScriptProducerKey final",
        "zc::Maybe<BuildScriptProducerKey> buildScriptProducerValue;",
    )
    for marker in required_producer_markers:
        if marker not in crate_key:
            errors.append(f"{CRATE_KEY}: missing stable build producer marker {marker}")
    if 'constexpr auto domain = "zom.build-script-producer"_zc;' not in (
        build_script_key_implementation
    ):
        errors.append(f"{BUILD_SCRIPT_KEY_IMPLEMENTATION}: invalid build producer domain")

    required_artifact_markers = (
        "class ArtifactFingerprint final",
        "ArtifactFingerprint BuildScriptOutputRecord::artifactFingerprint() const",
        'constexpr auto domain = "zom.build-script-output"_zc;',
    )
    combined_artifact_surface = build_script_key + build_script_key_implementation
    for marker in required_artifact_markers:
        if marker not in combined_artifact_surface:
            errors.append(f"{BUILD_SCRIPT_KEY}: missing artifact fingerprint marker {marker}")
    output_record = declaration_body(build_script_key, "class BuildScriptOutputRecord final")
    if output_record is None:
        errors.append(f"{BUILD_SCRIPT_KEY}: missing BuildScriptOutputRecord declaration")
    else:
        for marker in (
            "BuildScriptProducerKey producerKey() const noexcept;",
            "BuildScriptProducerKey producerValue;",
        ):
            if marker not in output_record:
                errors.append(f"{BUILD_SCRIPT_KEY}: build output record is missing {marker}")
        if "PreparatoryBuildScriptKey" in output_record:
            errors.append(
                f"{BUILD_SCRIPT_KEY}: build output record must retain only producer identity"
            )
    if "producerValue.encode(encoder);" not in build_script_key_implementation:
        errors.append(f"{BUILD_SCRIPT_KEY_IMPLEMENTATION}: output codec must encode producer key")

    generated_origin = declaration_body(source_key, "struct GeneratedFileSourceOrigin final")
    if generated_origin is None:
        errors.append(f"{SOURCE_KEY}: missing GeneratedFileSourceOrigin declaration")
    else:
        for marker in (
            "BuildScriptProducerKey producer;",
            "CanonicalRelativePath logicalPath;",
        ):
            if marker not in generated_origin:
                errors.append(f"{SOURCE_KEY}: generated source origin is missing {marker}")
        if re.search(r"\b(?:Sha256Digest|ArtifactFingerprint)\b", generated_origin):
            errors.append(f"{SOURCE_KEY}: generated source identity must not contain output content")
    if "acceptsContentDigest" in source_key:
        errors.append(f"{SOURCE_KEY}: source identity must not accept a content digest")
    for marker in (
        "return SourceOriginKey(GeneratedFileSourceOrigin{producer, zc::mv(logicalPath)});",
        "source.producer.encode(encoder);",
        "source.logicalPath.encode(encoder);",
    ):
        if marker not in source_key_implementation:
            errors.append(f"{SOURCE_KEY_IMPLEMENTATION}: missing generated source codec marker {marker}")

    module_key = declaration_body(source_key, "class ModuleKey final")
    if module_key is None:
        errors.append(f"{SOURCE_KEY}: missing ModuleKey declaration")
    else:
        for marker in ("CrateKey crateValue;", "zc::Vector<ModulePathSegment> pathValue;"):
            if marker not in module_key:
                errors.append(f"{SOURCE_KEY}: ModuleKey is missing stable field {marker}")
        if re.search(r"\b(?:SourceFileKey|SourceSpan)\b|\b(?:source|contains)\s*\(", module_key):
            errors.append(f"{SOURCE_KEY}: ModuleKey must contain only crate and canonical path")
    module_encode = function_body(source_key_implementation, "void ModuleKey::encode(")
    if module_encode is None:
        errors.append(f"{SOURCE_KEY_IMPLEMENTATION}: missing ModuleKey codec")
    elif "crateValue.encode(encoder);" not in module_encode or (
        "for (const auto& segment : pathValue)" not in module_encode
    ):
        errors.append(f"{SOURCE_KEY_IMPLEMENTATION}: ModuleKey codec must encode crate and path")

    structural_catalog_entry = declaration_body(
        module_resolution, "struct StructuralModuleCatalogEntry final"
    )
    if structural_catalog_entry is None or "identity::SourceFileKey source;" not in structural_catalog_entry:
        errors.append(f"{MODULE_RESOLUTION}: missing revision-local module source marker")

    if "const VerifiedBuildScriptPlan& buildPlan" not in package_request:
        errors.append(
            f"{PACKAGE_COMPILATION_REQUEST}: missing build-plan crate identity marker "
            "const VerifiedBuildScriptPlan& buildPlan"
        )
    if "const package::VerifiedBuildScriptPlan& buildPlan" not in crate_graph:
        errors.append(
            f"{CRATE_GRAPH}: missing build-plan crate identity marker "
            "const package::VerifiedBuildScriptPlan& buildPlan"
        )
    required_plan_markers = (
        "buildProducerFor(",
        "VerifiedCrateGraph::buildFinal(input.impl->request, input.impl->graph,",
    )
    combined_plan_surface = (
        package_request
        + package_request_implementation
        + crate_graph
        + crate_graph_implementation
        + session
    )
    for marker in required_plan_markers:
        if marker not in combined_plan_surface:
            errors.append(f"{CRATE_GRAPH}: missing build-plan crate identity marker {marker}")

    finalize_body = function_body(
        package_request_implementation,
        "VerifiedPackageCompilationRequest::finalizeRoots(",
    )
    if finalize_body is None:
        errors.append(f"{PACKAGE_COMPILATION_REQUEST_IMPLEMENTATION}: missing finalizeRoots body")
    elif re.search(r"\b(?:VerifiedBuildScriptResultSet|BuildScriptOutputRecord|ArtifactFingerprint)\b", finalize_body):
        errors.append(
            f"{PACKAGE_COMPILATION_REQUEST_IMPLEMENTATION}: final crate identity must not read build outputs"
        )


def check_compilation_unit_architecture(
    overrides: dict[Path, str], errors: list[str]
) -> None:
    compilation_unit = read_text(COMPILATION_UNIT_KEY, overrides)
    compilation_unit_implementation = read_text(
        COMPILATION_UNIT_KEY_IMPLEMENTATION, overrides
    )
    handle = read_text(HANDLE, overrides)
    crate_key = read_text(CRATE_KEY, overrides)
    source_key = read_text(SOURCE_KEY, overrides)
    invariant = read_text(IDENTITY_INVARIANT, overrides)
    fingerprint = read_text(SEMANTIC_CONTEXT_FINGERPRINT, overrides)
    identity_cmake = read_text(IDENTITY_CMAKE, overrides)

    required_markers = {
        COMPILATION_UNIT_KEY: (
            "enum class ToolchainComponent : uint8_t { Core = 0x01 };",
            "enum class CompilationUnitKind : uint8_t { UserPackage = 0x01, Toolchain = 0x02 };",
            "class ToolchainUnitKey final",
            "class CompilationUnitIdentity final",
            "static CompilationUnitIdentity userPackage(PackageKey&& package);",
            "static CompilationUnitIdentity toolchain(ToolchainUnitKey toolchain);",
        ),
        CRATE_KEY: (
            "static zc::Maybe<CrateKey> from(CompilationUnitIdentity&& unit,",
            "const CompilationUnitIdentity& unit() const noexcept;",
            "enum class CrateDependencyOriginKind : uint8_t { UserPackage = 0x01, ToolchainCore = 0x02 };",
            "class CrateDependencyOrigin final",
        ),
        SOURCE_KEY: (
            "CoreFile = 0x05",
            "struct CoreFileSourceOrigin final",
            "static SourceOriginKey coreFile(ToolchainUnitKey toolchain,",
        ),
        HANDLE: (
            "struct CompilationUnitIdentityTag final {};",
            "using CompilationUnitId = ContextHandle<CompilationUnitIdentityTag>;",
        ),
        IDENTITY_INVARIANT: (
            "CompilationUnit = 0x04",
            "CompilationUnitFreeze = 0x04",
        ),
        SEMANTIC_CONTEXT_FINGERPRINT: (
            "class ToolchainSemanticContextInput final",
            "zc::ArrayPtr<const CompilationUnitIdentity> compilationUnits,",
            "zc::ArrayPtr<const ToolchainSemanticContextInput> toolchainInputs,",
        ),
        IDENTITY_CMAKE: (
            "${CMAKE_CURRENT_SOURCE_DIR}/key/compilation-unit-key.cc",
        ),
    }
    text_by_path = {
        COMPILATION_UNIT_KEY: compilation_unit,
        CRATE_KEY: crate_key,
        SOURCE_KEY: source_key,
        HANDLE: handle,
        IDENTITY_INVARIANT: invariant,
        SEMANTIC_CONTEXT_FINGERPRINT: fingerprint,
        IDENTITY_CMAKE: identity_cmake,
    }
    for path, markers in required_markers.items():
        for marker in markers:
            if marker not in text_by_path[path]:
                errors.append(f"{path}: missing compilation-unit cutover marker {marker}")

    for marker in (
        'constexpr auto kToolchainUnitKeyDomain = "zom.toolchain-core-key"_zc;',
        "bytes.add(0x00);",
        "unit.toolchain.encode(encoder);",
    ):
        if marker not in compilation_unit_implementation:
            errors.append(
                f"{COMPILATION_UNIT_KEY_IMPLEMENTATION}: missing toolchain-unit codec marker {marker}"
            )

    forbidden_patterns = (
        re.compile(r"\bidentity::PackageId\b"),
        re.compile(r"\bPackageRegistry\b"),
        re.compile(r"\bcollectPackage\s*\("),
        re.compile(r"\bfreezePackages\s*\("),
        re.compile(r"\bCrateKey::package\s*\("),
        re.compile(r"\.packageEdge\s*\("),
        re.compile(r"CrateKey::from\s*\(\s*PackageKey"),
    )
    for pattern in forbidden_patterns:
        for path in sorted(matching_files(pattern, overrides)):
            errors.append(
                f"{path}: package-only semantic identity surface is forbidden: {pattern.pattern}"
            )


def check_semantic_type_store_architecture(
    overrides: dict[Path, str], errors: list[str]
) -> None:
    session = read_text(COMPILER_SESSION, overrides)
    package_request = read_text(PACKAGE_COMPILATION_REQUEST, overrides)
    store = read_text(SEMANTIC_TYPE_STORE, overrides)
    type_key = read_text(SEMANTIC_TYPE_KEY, overrides)

    required_session_markers = (
        "class CompilerSessionSemanticContextResources final\n"
        "    : public module_graph_query::ModuleGraphIdentityMaterializationResources",
        "issueSemanticTypeStoreConstructionToken(resources->contextBrand)",
        "zc::Own<type::SemanticTypeStore> semanticTypeStore;",
        "zc::Own<type::SemanticTypeStore>& semanticTypeStore;",
    )
    for marker in required_session_markers:
        if marker not in session:
            errors.append(f"{COMPILER_SESSION}: missing semantic type store owner marker {marker}")

    if session.count("finalSealedSnapshot = zc::mv(admitted).takeSnapshot();") != 1:
        errors.append(
            f"{COMPILER_SESSION}: final sealed snapshot must publish exactly once"
        )

    if "ZC_DISALLOW_COPY_AND_MOVE(SemanticTypeStore);" not in store:
        errors.append(f"{SEMANTIC_TYPE_STORE}: semantic type store must be pinned")
    required_admission_markers = (
        "const identity::IdentityInternerSet& identities",
        "SemanticTypeAdmissionResult canonicalizeClosed(semantic::TypeData&& data) const",
        "definitionKeyForAdmission(",
        "validateGenericParameterForAdmission(",
    )
    for marker in required_admission_markers:
        if marker not in store:
            errors.append(f"{SEMANTIC_TYPE_STORE}: missing store-bound admission marker {marker}")
    if "identity::SemanticContextBrand admissionContext;" not in type_key:
        errors.append(f"{SEMANTIC_TYPE_KEY}: canonical type data must retain admission provenance")


def check_scalar_literal_authority_architecture(
    overrides: dict[Path, str], errors: list[str]
) -> None:
    header = read_text(SCALAR_LITERAL_FACTS, overrides)
    implementation = read_text(SCALAR_LITERAL_FACTS_IMPLEMENTATION, overrides)

    if '#include "zomlang/compiler/checker/checker-identity-authority.h"' not in header:
        errors.append(f"{SCALAR_LITERAL_FACTS}: missing checker identity authority dependency")
    if "const CheckerIdentityAuthority& identities;" not in header:
        errors.append(f"{SCALAR_LITERAL_FACTS}: scalar facts must retain checker identity authority")
    for path, text in (
        (SCALAR_LITERAL_FACTS, header),
        (SCALAR_LITERAL_FACTS_IMPLEMENTATION, implementation),
    ):
        if "SemanticIdentityRegistrySet" in text:
            errors.append(f"{path}: scalar facts must not depend on frozen identity registries")
    for marker in (
        "input.identities.semanticContext() != input.semanticContext",
        "input.identities.module(input.module) == zc::none",
        "input.identities.sourceFile(input.source) != zc::none",
    ):
        if marker not in implementation:
            errors.append(
                f"{SCALAR_LITERAL_FACTS_IMPLEMENTATION}: missing scalar authority admission marker {marker}"
            )


def check_checker_diagnostic_authority_architecture(
    overrides: dict[Path, str], errors: list[str]
) -> None:
    header = read_text(CHECKER_DIAGNOSTIC_ADAPTER, overrides)
    implementation = read_text(CHECKER_DIAGNOSTIC_ADAPTER_IMPLEMENTATION, overrides)

    for marker in (
        '#include "zomlang/compiler/checker/checker-identity-authority.h"',
        "const CheckerIdentityAuthority& identities",
    ):
        if marker not in header:
            errors.append(f"{CHECKER_DIAGNOSTIC_ADAPTER}: missing checker authority marker {marker}")
    for path, text in (
        (CHECKER_DIAGNOSTIC_ADAPTER, header),
        (CHECKER_DIAGNOSTIC_ADAPTER_IMPLEMENTATION, implementation),
    ):
        if "SemanticIdentityRegistrySet" in text:
            errors.append(f"{path}: checker diagnostic rendering must not depend on frozen registries")
    for marker in (
        "identities.definition(definition)",
        "identities.definition(key)",
        "identities.genericParameter(key)",
    ):
        if marker not in implementation:
            errors.append(
                f"{CHECKER_DIAGNOSTIC_ADAPTER_IMPLEMENTATION}: missing diagnostic authority lookup {marker}"
            )


def check_borrow_interface_authority_architecture(
    overrides: dict[Path, str], errors: list[str]
) -> None:
    header = read_text(BORROW_INTERFACE, overrides)
    implementation = read_text(BORROW_INTERFACE_IMPLEMENTATION, overrides)

    for marker in (
        '#include "zomlang/compiler/checker/checker-identity-authority.h"',
        "const CheckerIdentityAuthority& identities;",
        "const CheckerIdentityAuthority& identities);",
    ):
        if marker not in header:
            errors.append(f"{BORROW_INTERFACE}: missing borrow authority marker {marker}")
    for path, text in (
        (BORROW_INTERFACE, header),
        (BORROW_INTERFACE_IMPLEMENTATION, implementation),
    ):
        if "SemanticIdentityRegistrySet" in text:
            errors.append(f"{path}: borrow interface must not depend on frozen registries")
    for marker in (
        "identities.definition(summary.callable)",
        "input.identities.module(input.module) == zc::none",
        "input.identities.module(input.module)",
    ):
        if marker not in implementation:
            errors.append(
                f"{BORROW_INTERFACE_IMPLEMENTATION}: missing borrow authority lookup {marker}"
            )


def analyze(
    manifest: dict[str, object], overrides: dict[Path, str] | None = None
) -> list[str]:
    active_overrides = overrides or {}
    errors: list[str] = []
    check_manifest_shape(manifest, errors)
    variants = load_schema_variants(active_overrides)
    check_schema_coverage(manifest, variants, errors)
    check_live_producers(manifest, active_overrides, errors)
    check_no_post_parse_expansion(manifest, active_overrides, errors)
    check_pointer_identity_allowlist(manifest, active_overrides, errors)
    check_header_wire_inventory(active_overrides, errors)
    check_canonical_header_type_inventory(active_overrides, errors)
    check_canonical_header_type_producer(active_overrides, errors)
    check_canonical_definition_header_producer(active_overrides, errors)
    check_canonical_impl_header_producer(active_overrides, errors)
    check_canonical_overload_header_inventory(active_overrides, errors)
    check_overload_header_digest_inventory(active_overrides, errors)
    check_canonical_impl_header_inventory(active_overrides, errors)
    check_module_resolution_key_architecture(active_overrides, errors)
    check_semantic_import_binding_key_architecture(active_overrides, errors)
    check_stable_identity_architecture(active_overrides, errors)
    check_compilation_unit_architecture(active_overrides, errors)
    check_semantic_type_store_architecture(active_overrides, errors)
    check_scalar_literal_authority_architecture(active_overrides, errors)
    check_checker_diagnostic_authority_architecture(active_overrides, errors)
    check_borrow_interface_authority_architecture(active_overrides, errors)
    return errors


def run_check() -> int:
    errors = analyze(load_manifest())
    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1
    print("identity architecture check passed")
    return 0


def run_self_test() -> int:
    baseline = load_manifest()
    baseline_errors = analyze(baseline)
    if baseline_errors:
        for error in baseline_errors:
            print(f"error: baseline: {error}", file=sys.stderr)
        return 1

    cases: list[tuple[str, dict[str, object], dict[Path, str], str]] = []

    missing_rule = copy.deepcopy(baseline)
    missing_rule["producers"].pop("FunctionDecl")  # type: ignore[index,union-attr]
    cases.append(("missing schema rule", missing_rule, {}, "FunctionDecl has no identity rule"))

    missing_parser = copy.deepcopy(baseline)
    missing_parser["producers"]["FunctionDecl"]["parser"] = "expression-parser.cc"  # type: ignore[index]
    cases.append(("missing parser producer", missing_parser, {}, "makeFunctionDecl is missing"))

    inventory_text = (ROOT / INVENTORY).read_text(encoding="utf-8")
    cases.append(
        (
            "missing inventory handler",
            copy.deepcopy(baseline),
            {INVENTORY: inventory_text.replace("case ast::SyntaxKind::FunctionDecl:", "")},
            "inventory handler for FunctionDecl is missing",
        )
    )

    unknown_kind = copy.deepcopy(baseline)
    unknown_kind["producers"]["FunctionDecl"]["identities"] = ["UnknownDefinition"]  # type: ignore[index]
    cases.append(("unknown identity kind", unknown_kind, {}, "unknown identity UnknownDefinition"))

    cases.append(
        (
            "post-parse producer",
            copy.deepcopy(baseline),
            {INVENTORY: inventory_text + "\nvoid fixture() { builder.makeFunctionDecl(); }\n"},
            "post-parse semantic producer makeFunctionDecl is forbidden",
        )
    )

    session_text = (ROOT / COMPILER_SESSION).read_text(encoding="utf-8")
    interner_text = (ROOT / CANONICAL_IDENTITY_INTERNER).read_text(encoding="utf-8")
    cases.append(
        (
            "missing callable parameter interner",
            copy.deepcopy(baseline),
            {
                CANONICAL_IDENTITY_INTERNER: interner_text.replace(
                    "using CallableParameterIdentityEntry =",
                    "using MissingCallableParameterIdentityEntry =",
                    1,
                )
            },
            "missing arena interner marker using CallableParameterIdentityEntry =",
        )
    )
    cases.append(
        (
            "missing definition key interner lookup",
            copy.deepcopy(baseline),
            {
                CANONICAL_IDENTITY_INTERNER: interner_text.replace(
                    "const DefinitionKey& key) const;",
                    "const MissingDefinitionKey& key) const;",
                    1,
                )
            },
            "missing arena interner marker const DefinitionKey& key) const;",
        )
    )
    cases.append(
        (
            "missing semantic type store construction",
            copy.deepcopy(baseline),
            {
                COMPILER_SESSION: session_text.replace(
                    "issueSemanticTypeStoreConstructionToken(resources->contextBrand)",
                    "missingSemanticTypeStoreConstruction(resources->contextBrand)",
                )
            },
            "missing semantic type store owner marker",
        )
    )

    scalar_literal_header_text = (ROOT / SCALAR_LITERAL_FACTS).read_text(encoding="utf-8")
    scalar_literal_implementation_text = (ROOT / SCALAR_LITERAL_FACTS_IMPLEMENTATION).read_text(
        encoding="utf-8"
    )
    cases.append(
        (
            "scalar literal authority removed",
            copy.deepcopy(baseline),
            {
                SCALAR_LITERAL_FACTS: scalar_literal_header_text.replace(
                    "const CheckerIdentityAuthority& identities;",
                    "const MissingCheckerIdentityAuthority& identities;",
                    1,
                )
            },
            "scalar facts must retain checker identity authority",
        )
    )
    cases.append(
        (
            "scalar literal registry fallback",
            copy.deepcopy(baseline),
            {
                SCALAR_LITERAL_FACTS_IMPLEMENTATION: scalar_literal_implementation_text.replace(
                    "FactEmissionResult FactEmitter::emit(const FactEmissionInput& input) {",
                    "SemanticIdentityRegistrySet forbiddenRegistry;\n"
                    "FactEmissionResult FactEmitter::emit(const FactEmissionInput& input) {",
                    1,
                )
            },
            "scalar facts must not depend on frozen identity registries",
        )
    )

    checker_diagnostic_adapter_text = (ROOT / CHECKER_DIAGNOSTIC_ADAPTER).read_text(
        encoding="utf-8"
    )
    checker_diagnostic_adapter_implementation_text = (
        ROOT / CHECKER_DIAGNOSTIC_ADAPTER_IMPLEMENTATION
    ).read_text(encoding="utf-8")
    cases.append(
        (
            "checker diagnostic authority removed",
            copy.deepcopy(baseline),
            {
                CHECKER_DIAGNOSTIC_ADAPTER: checker_diagnostic_adapter_text.replace(
                    "CheckerIdentityAuthority", "MissingCheckerIdentityAuthority"
                )
            },
            "missing checker authority marker const CheckerIdentityAuthority& identities",
        )
    )
    cases.append(
        (
            "checker diagnostic registry fallback",
            copy.deepcopy(baseline),
            {
                CHECKER_DIAGNOSTIC_ADAPTER_IMPLEMENTATION: (
                    checker_diagnostic_adapter_implementation_text.replace(
                        "void appendDefinition(zc::Vector<char>& output, identity::DefId definition,",
                        "SemanticIdentityRegistrySet forbiddenRegistry;\n"
                        "void appendDefinition(zc::Vector<char>& output, identity::DefId definition,",
                        1,
                    )
                )
            },
            "checker diagnostic rendering must not depend on frozen registries",
        )
    )

    borrow_interface_text = (ROOT / BORROW_INTERFACE).read_text(encoding="utf-8")
    borrow_interface_implementation_text = (ROOT / BORROW_INTERFACE_IMPLEMENTATION).read_text(
        encoding="utf-8"
    )
    cases.append(
        (
            "borrow interface authority removed",
            copy.deepcopy(baseline),
            {
                BORROW_INTERFACE: borrow_interface_text.replace(
                    "const CheckerIdentityAuthority& identities;",
                    "const MissingCheckerIdentityAuthority& identities;",
                    1,
                )
            },
            "missing borrow authority marker const CheckerIdentityAuthority& identities;",
        )
    )
    cases.append(
        (
            "borrow interface registry fallback",
            copy.deepcopy(baseline),
            {
                BORROW_INTERFACE_IMPLEMENTATION: borrow_interface_implementation_text.replace(
                    "BorrowInterfaceBuildResult BorrowInterfaceBuilder::build(",
                    "SemanticIdentityRegistrySet forbiddenRegistry;\n"
                    "BorrowInterfaceBuildResult BorrowInterfaceBuilder::build(",
                    1,
                )
            },
            "borrow interface must not depend on frozen registries",
        )
    )

    cases.append(
        (
            "missing canonical interner owner",
            copy.deepcopy(baseline),
            {
                COMPILER_SESSION: session_text.replace(
                    "zc::Maybe<identity::IdentityInternerSet> identityInternerSet;",
                    "zc::Maybe<identity::MissingCanonicalIdentityInternerSet> identityInternerSet;",
                    1,
                )
            },
            "missing arena-owned identity interner marker",
        )
    )

    fingerprint_text = (ROOT / SEMANTIC_CONTEXT_FINGERPRINT).read_text(encoding="utf-8")
    cases.append(
        (
            "missing toolchain fingerprint lineage",
            copy.deepcopy(baseline),
            {
                SEMANTIC_CONTEXT_FINGERPRINT: fingerprint_text.replace(
                    "class ToolchainSemanticContextInput final",
                    "class MissingToolchainSemanticContextInput final",
                    1,
                )
            },
            "missing compilation-unit cutover marker class ToolchainSemanticContextInput final",
        )
    )

    crate_key_text = (ROOT / CRATE_KEY).read_text(encoding="utf-8")
    cases.append(
        (
            "package handle semantic fallback",
            copy.deepcopy(baseline),
            {CRATE_KEY: crate_key_text + "\nidentity::PackageId forbiddenPackageHandle;\n"},
            "package-only semantic identity surface is forbidden",
        )
    )

    package_request_text = (ROOT / PACKAGE_COMPILATION_REQUEST).read_text(encoding="utf-8")
    cases.append(
        (
            "missing build-plan crate finalization",
            copy.deepcopy(baseline),
            {
                PACKAGE_COMPILATION_REQUEST: package_request_text.replace(
                    "const VerifiedBuildScriptPlan& buildPlan",
                    "const MissingBuildScriptPlan& buildPlan",
                )
            },
            "missing build-plan crate identity marker",
        )
    )

    cases.append(
        (
            "output-derived build identity",
            copy.deepcopy(baseline),
            {CRATE_KEY: crate_key_text + "\nclass BuildScriptOutputKey;\n"},
            "output-derived BuildScriptOutputKey identity is forbidden",
        )
    )

    build_script_implementation_text = (ROOT / BUILD_SCRIPT_KEY_IMPLEMENTATION).read_text(
        encoding="utf-8"
    )
    cases.append(
        (
            "mutated build producer domain",
            copy.deepcopy(baseline),
            {
                BUILD_SCRIPT_KEY_IMPLEMENTATION: build_script_implementation_text.replace(
                    '"zom.build-script-producer"_zc',
                    '"zom.build-script-producer.mutated"_zc',
                )
            },
            "invalid build producer domain",
        )
    )

    build_script_key_text = (ROOT / BUILD_SCRIPT_KEY).read_text(encoding="utf-8")
    cases.append(
        (
            "preparatory key retained in output record",
            copy.deepcopy(baseline),
            {
                BUILD_SCRIPT_KEY: build_script_key_text.replace(
                    "BuildScriptProducerKey producerValue;",
                    "BuildScriptProducerKey producerValue;\n"
                    "  PreparatoryBuildScriptKey preparatoryValue;",
                )
            },
            "build output record must retain only producer identity",
        )
    )

    source_key_text = (ROOT / SOURCE_KEY).read_text(encoding="utf-8")
    cases.append(
        (
            "generated source content identity",
            copy.deepcopy(baseline),
            {
                SOURCE_KEY: source_key_text.replace(
                    "BuildScriptProducerKey producer;",
                    "BuildScriptProducerKey producer;\n  Sha256Digest contentDigest;",
                    1,
                )
            },
            "generated source identity must not contain output content",
        )
    )
    cases.append(
        (
            "position-derived module identity",
            copy.deepcopy(baseline),
            {
                SOURCE_KEY: source_key_text.replace(
                    "CrateKey crateValue;\n  zc::Vector<ModulePathSegment> pathValue;",
                    "CrateKey crateValue;\n  SourceSpan declarationSpan;\n"
                    "  zc::Vector<ModulePathSegment> pathValue;",
                )
            },
            "ModuleKey must contain only crate and canonical path",
        )
    )

    module_resolution_text = (ROOT / MODULE_RESOLUTION).read_text(encoding="utf-8")
    cases.append(
        (
            "missing module source projection",
            copy.deepcopy(baseline),
            {
                MODULE_RESOLUTION: module_resolution_text.replace(
                    "identity::ModuleId module;\n  identity::SourceFileKey source;",
                    "identity::ModuleId module;\n  identity::MissingSourceFileKey source;",
                    1,
                )
            },
            "missing revision-local module source marker",
        )
    )

    cases.append(
        (
            "missing final sealed publication",
            copy.deepcopy(baseline),
            {
                COMPILER_SESSION: session_text.replace(
                    "finalSealedSnapshot = zc::mv(admitted).takeSnapshot();",
                    "finalSealedSnapshot = zc::mv(rejected).takeSnapshot();",
                    1,
                )
            },
            "final sealed snapshot must publish exactly once",
        )
    )

    module_resolution_key_text = (ROOT / MODULE_RESOLUTION_KEY).read_text(encoding="utf-8")
    module_resolution_key_implementation_text = (
        ROOT / MODULE_RESOLUTION_KEY_IMPLEMENTATION
    ).read_text(encoding="utf-8")
    semantic_import_binding_key_text = (ROOT / SEMANTIC_IMPORT_BINDING_KEY).read_text(
        encoding="utf-8"
    )
    semantic_import_binding_key_implementation_text = (
        ROOT / SEMANTIC_IMPORT_BINDING_KEY_IMPLEMENTATION
    ).read_text(encoding="utf-8")
    cases.append(
        (
            "semantic import operation tag drift",
            copy.deepcopy(baseline),
            {
                SEMANTIC_IMPORT_BINDING_KEY: semantic_import_binding_key_text.replace(
                    "ForeignReexport = 0x02", "ForeignReexport = 0x03"
                )
            },
            "SemanticImportOperation must retain its three exact semantic tags",
        )
    )
    cases.append(
        (
            "semantic import key field drift",
            copy.deepcopy(baseline),
            {
                SEMANTIC_IMPORT_BINDING_KEY: semantic_import_binding_key_text.replace(
                    "DeclaredDefinitionName localNameValue;",
                    "DeclaredDefinitionName mutatedLocalNameValue;",
                )
            },
            "ImportBindingKey must contain exactly seven semantic fields",
        )
    )
    cases.append(
        (
            "semantic import provenance field",
            copy.deepcopy(baseline),
            {
                SEMANTIC_IMPORT_BINDING_KEY: semantic_import_binding_key_text.replace(
                    "  DeclaredDefinitionName localNameValue;\n};",
                    "  DeclaredDefinitionName localNameValue;\n  NodeId siteValue;\n};",
                )
            },
            "provenance handles and revisions are forbidden",
        )
    )
    cases.append(
        (
            "semantic import codec order drift",
            copy.deepcopy(baseline),
            {
                SEMANTIC_IMPORT_BINDING_KEY_IMPLEMENTATION: (
                    semantic_import_binding_key_implementation_text.replace(
                        "  record.encodeUint8(static_cast<uint8_t>(sourceNamespaceValue));\n"
                        "  sourceNameValue.encode(record);",
                        "  sourceNameValue.encode(record);\n"
                        "  record.encodeUint8(static_cast<uint8_t>(sourceNamespaceValue));",
                    )
                )
            },
            "semantic import key codec must preserve canonical field order",
        )
    )
    cases.append(
        (
            "semantic import codec domain drift",
            copy.deepcopy(baseline),
            {
                SEMANTIC_IMPORT_BINDING_KEY_IMPLEMENTATION: (
                    semantic_import_binding_key_implementation_text.replace(
                        '"zom.semantic-import-binding"_zc',
                        '"zom.semantic-import-binding.mutated"_zc',
                    )
                )
            },
            "invalid semantic import key domain",
        )
    )
    cases.append(
        (
            "module resolution policy field drift",
            copy.deepcopy(baseline),
            {
                MODULE_RESOLUTION_KEY: module_resolution_key_text.replace(
                    "ModuleCandidateSelectionPolicy candidateSelectionValue;",
                    "ModuleCandidateSelectionPolicy mutatedSelectionValue;",
                )
            },
            "must contain exactly eight policy fields",
        )
    )
    cases.append(
        (
            "module resolution request field drift",
            copy.deepcopy(baseline),
            {
                MODULE_RESOLUTION_KEY: module_resolution_key_text.replace(
                    "ModuleDependencyKind kindValue;",
                    "ModuleDependencyKind mutatedKindValue;",
                )
            },
            "must contain exactly five semantic fields",
        )
    )
    cases.append(
        (
            "module resolution policy tag drift",
            copy.deepcopy(baseline),
            {
                MODULE_RESOLUTION_KEY: module_resolution_key_text.replace(
                    "enum class UnicodeNormalizationPolicy : uint8_t { Nfc = 0x01 };",
                    "enum class UnicodeNormalizationPolicy : uint8_t { Nfc = 0x02 };",
                )
            },
            "UnicodeNormalizationPolicy must retain its exact RFC 0018 tag",
        )
    )
    cases.append(
        (
            "module dependency tag drift",
            copy.deepcopy(baseline),
            {
                MODULE_RESOLUTION_KEY: module_resolution_key_text.replace(
                    "Prelude = 0x04", "Prelude = 0x05"
                )
            },
            "ModuleDependencyKind must retain its four exact tags",
        )
    )
    cases.append(
        (
            "module catalog bucket domain drift",
            copy.deepcopy(baseline),
            {
                MODULE_RESOLUTION_KEY_IMPLEMENTATION: (
                    module_resolution_key_implementation_text.replace(
                        '"zom.module-catalog-path-bucket"_zc',
                        '"zom.module-catalog-path-bucket.mutated"_zc',
                    )
                )
            },
            "invalid catalog bucket domain",
        )
    )
    cases.append(
        (
            "module resolution policy domain drift",
            copy.deepcopy(baseline),
            {
                MODULE_RESOLUTION_KEY_IMPLEMENTATION: (
                    module_resolution_key_implementation_text.replace(
                        '"zom.module-resolution-policy"_zc',
                        '"zom.module-resolution-policy.mutated"_zc',
                    )
                )
            },
            "invalid resolution policy domain",
        )
    )
    cases.append(
        (
            "module resolution request domain drift",
            copy.deepcopy(baseline),
            {
                MODULE_RESOLUTION_KEY_IMPLEMENTATION: (
                    module_resolution_key_implementation_text.replace(
                        '"zom.module-resolution"_zc',
                        '"zom.module-resolution.mutated"_zc',
                    )
                )
            },
            "invalid resolution request domain",
        )
    )
    for name, signature, expected in (
        (
            "module catalog bucket decoder removed",
            "ModuleCatalogPathBucketKey::decodeCanonical(",
            "catalog bucket decoder must be exact bounded and canonical",
        ),
        (
            "requester ancestry decoder removed",
            "RequesterModuleAncestry::decodeCanonical(",
            "requester ancestry decoder must be exact bounded and structural",
        ),
        (
            "module catalog bucket value decoder removed",
            "ModuleCatalogPathBucket::decodeCanonical(",
            "catalog bucket value decoder must validate exact optional membership",
        ),
        (
            "module resolution policy decoder removed",
            "ModuleResolutionPolicyKey::decodeCanonical(",
            "resolution policy decoder must close all eight fields",
        ),
        (
            "module resolution request decoder removed",
            "ModuleResolutionKey::decodeCanonical(",
            "resolution request decoder must be exact bounded and compositional",
        ),
    ):
        cases.append(
            (
                name,
                copy.deepcopy(baseline),
                {
                    MODULE_RESOLUTION_KEY_IMPLEMENTATION: (
                        module_resolution_key_implementation_text.replace(
                            signature,
                            signature.replace("::decodeCanonical(", "::removedDecodeCanonical("),
                            1,
                        )
                    )
                },
                expected,
            )
        )
    cases.append(
        (
            "module catalog empty path admitted",
            copy.deepcopy(baseline),
            {
                MODULE_RESOLUTION_KEY_IMPLEMENTATION: (
                    module_resolution_key_implementation_text.replace(
                        "if (path.size() == 0) { return zc::none; }", "", 1
                    )
                )
            },
            "catalog bucket must reject an empty path",
        )
    )
    cases.append(
        (
            "requester ancestry field drift",
            copy.deepcopy(baseline),
            {
                MODULE_RESOLUTION_KEY: module_resolution_key_text.replace(
                    "zc::Vector<ModuleKey> ancestryValue;",
                    "zc::Vector<ModuleKey> mutatedAncestryValue;",
                )
            },
            "RequesterModuleAncestry must contain exactly requester and ancestry",
        )
    )
    cases.append(
        (
            "requester ancestry admits an empty chain",
            copy.deepcopy(baseline),
            {
                MODULE_RESOLUTION_KEY_IMPLEMENTATION: (
                    module_resolution_key_implementation_text.replace(
                        "ancestry.size() == 0 || !sameKey(requester, ancestry[0])",
                        "false",
                        1,
                    )
                )
            },
            "requester ancestry must reject an empty or requester-mismatched chain",
        )
    )
    cases.append(
        (
            "requester ancestry admits skipped parents",
            copy.deepcopy(baseline),
            {
                MODULE_RESOLUTION_KEY_IMPLEMENTATION: (
                    module_resolution_key_implementation_text.replace(
                        "child.path().size() != parent.path().size() + 1",
                        "false",
                        1,
                    )
                )
            },
            "requester ancestry must require strict lexical parents",
        )
    )
    cases.append(
        (
            "catalog bucket value field drift",
            copy.deepcopy(baseline),
            {
                MODULE_RESOLUTION_KEY: module_resolution_key_text.replace(
                    "zc::Maybe<ModuleKey> moduleValue;",
                    "zc::Maybe<ModuleKey> mutatedModuleValue;",
                )
            },
            "ModuleCatalogPathBucket must contain exactly key and optional module",
        )
    )
    cases.append(
        (
            "catalog bucket admits a mismatched module",
            copy.deepcopy(baseline),
            {
                MODULE_RESOLUTION_KEY_IMPLEMENTATION: (
                    module_resolution_key_implementation_text.replace(
                        "!sameCrate(key.crate(), module.crate()) || "
                        "!samePath(key.path(), module.path())",
                        "false",
                        1,
                    )
                )
            },
            "present catalog bucket must require exact crate and path equality",
        )
    )

    request_field_anchor = "  ModuleResolutionPolicyKey policyValue;\n};"
    forbidden_request_fields = (
        ("source span", "SourceSpan sourceSpanValue;", "provenance fields are forbidden"),
        ("source file", "SourceFileKey sourceFileValue;", "provenance fields are forbidden"),
        ("AST node", "NodeId syntaxNodeValue;", "provenance fields are forbidden"),
        (
            "requested target",
            "ModuleKey requestedTargetValue;",
            "requested target fields are forbidden",
        ),
        (
            "environment",
            "EnvironmentFingerprint environmentValue;",
            "environment or revision fields are forbidden",
        ),
        (
            "revision",
            "uint64_t revisionValue;",
            "environment or revision fields are forbidden",
        ),
    )
    for field_name, field, expected in forbidden_request_fields:
        cases.append(
            (
                f"module resolution request {field_name} exclusion",
                copy.deepcopy(baseline),
                {
                    MODULE_RESOLUTION_KEY: module_resolution_key_text.replace(
                        request_field_anchor,
                        f"  ModuleResolutionPolicyKey policyValue;\n  {field}\n}};",
                    )
                },
                expected,
            )
        )

    module_resolution_text = (ROOT / MODULE_RESOLUTION).read_text(encoding="utf-8")
    module_resolution_implementation_text = (
        ROOT / MODULE_RESOLUTION_IMPLEMENTATION
    ).read_text(encoding="utf-8")
    cases.append(
        (
            "duplicate binder module dependency enum",
            copy.deepcopy(baseline),
            {
                MODULE_RESOLUTION: module_resolution_text
                + "\nenum class ModuleDependencyKind : uint8_t { Import = 0x01 };\n"
            },
            "ModuleDependencyKind must have one identity-layer owner",
        )
    )
    cases.append(
        (
            "obsolete requester ancestry surface",
            copy.deepcopy(baseline),
            {
                MODULE_RESOLUTION: module_resolution_text
                + "\nclass ModuleRequesterAncestry final {};\n"
            },
            "obsolete ModuleRequesterAncestry identity surface is forbidden",
        )
    )
    cases.append(
        (
            "resolver drops admitted catalog buckets",
            copy.deepcopy(baseline),
            {
                MODULE_RESOLUTION_IMPLEMENTATION: module_resolution_implementation_text.replace(
                    "  zc::Vector<identity::ModuleCatalogPathBucket> catalogBuckets;\n",
                    "",
                    1,
                )
            },
            "resolver must retain admitted exact input",
        )
    )
    cases.append(
        (
            "batch structural resolution authority",
            copy.deepcopy(baseline),
            {
                MODULE_RESOLUTION: module_resolution_text
                + "\nModulePathResolution resolve(ModuleDependencyRequest&&);\n"
            },
            "batch structural resolution authority is forbidden",
        )
    )

    identity_cmake_text = (ROOT / IDENTITY_CMAKE).read_text(encoding="utf-8")
    cases.append(
        (
            "missing canonical type encode source registration",
            copy.deepcopy(baseline),
            {
                IDENTITY_CMAKE: identity_cmake_text.replace(
                    "  ${CMAKE_CURRENT_SOURCE_DIR}/canonical/header-type-encode.cc\n", ""
                )
            },
            "missing canonical header type source canonical/header-type-encode.cc",
        )
    )
    canonical_type_encode_text = (ROOT / CANONICAL_HEADER_TYPE_ENCODE).read_text(
        encoding="utf-8"
    )
    cases.append(
        (
            "missing canonical type tag encoding",
            copy.deepcopy(baseline),
            {
                CANONICAL_HEADER_TYPE_ENCODE: canonical_type_encode_text.replace(
                    "encoder.encodeUint8(static_cast<uint8_t>(typeKind));",
                    "encoder.missingTypeTag(static_cast<uint8_t>(typeKind));",
                )
            },
            "canonical type tag must encode first",
        )
    )
    cases.append(
        (
            "missing canonical function result field encoding",
            copy.deepcopy(baseline),
            {
                CANONICAL_HEADER_TYPE_ENCODE: canonical_type_encode_text.replace(
                    "value.result.encode(encoder);", "value.result.missingEncode(encoder);"
                )
            },
            "missing field codec marker value.result.encode(encoder);",
        )
    )
    cases.append(
        (
            "length-wrapped canonical type record",
            copy.deepcopy(baseline),
            {
                CANONICAL_HEADER_TYPE_ENCODE: canonical_type_encode_text.replace(
                    "encoder.encodeUint8(static_cast<uint8_t>(typeKind));",
                    "encoder.encodeUint8(static_cast<uint8_t>(typeKind));\n"
                    "  encoder.encodeByteString(zc::ArrayPtr<const uint8_t>());",
                )
            },
            "nested canonical type records must encode inline",
        )
    )
    canonical_type_test_text = (ROOT / CANONICAL_HEADER_TYPE_TEST).read_text(encoding="utf-8")
    cases.append(
        (
            "missing dynamic-array versus slice identity test",
            copy.deepcopy(baseline),
            {
                CANONICAL_HEADER_TYPE_TEST: canonical_type_test_text.replace(
                    'expectHex(dynamicArray, "070201"_zc);',
                    'expectHex(dynamicArray, "080201"_zc);',
                )
            },
            "missing canonical type test marker",
        )
    )

    canonical_type_producer_text = (
        ROOT / CANONICAL_HEADER_TYPE_PRODUCER_IMPLEMENTATION
    ).read_text(encoding="utf-8")
    canonical_type_producer_test_text = (
        ROOT / CANONICAL_HEADER_TYPE_PRODUCER_TEST
    ).read_text(encoding="utf-8")
    cases.append(
        (
            "missing slice AST producer mapping",
            copy.deepcopy(baseline),
            {
                CANONICAL_HEADER_TYPE_PRODUCER_IMPLEMENTATION: (
                    canonical_type_producer_text.replace(
                        "case ast::SyntaxKind::SliceArrayTypeExpr:",
                        "case ast::SyntaxKind::ArrayTypeExpr:",
                        1,
                    )
                )
            },
            "missing AST mapping for SliceArrayTypeExpr",
        )
    )
    cases.append(
        (
            "generic binder depth search removed",
            copy.deepcopy(baseline),
            {
                CANONICAL_HEADER_TYPE_PRODUCER_IMPLEMENTATION: (
                    canonical_type_producer_text.replace(
                        "for (size_t depth = 0; depth < binders.size(); ++depth)",
                        "for (size_t depth = binders.size(); depth < binders.size(); ++depth)",
                        1,
                    )
                )
            },
            "producer must search lexical generic binders by stable-owner depth",
        )
    )
    cases.append(
        (
            "fixed array overflow check removed",
            copy.deepcopy(baseline),
            {
                CANONICAL_HEADER_TYPE_PRODUCER_IMPLEMENTATION: (
                    canonical_type_producer_text.replace(
                        "value > (UINT64_MAX - digit) / base",
                        "false",
                        1,
                    )
                )
            },
            "producer must reject fixed-array length overflow",
        )
    )
    cases.append(
        (
            "canonical type producer fixed vector drift",
            copy.deepcopy(baseline),
            {
                CANONICAL_HEADER_TYPE_PRODUCER_TEST: canonical_type_producer_test_text.replace(
                    '"0103000000000000000000000000000000000000000000000000"_zc',
                    '"mutated"_zc',
                    1,
                )
            },
            "missing producer test marker",
        )
    )
    cases.append(
        (
            "canonical type producer duplicate binder admission drift",
            copy.deepcopy(baseline),
            {
                CANONICAL_HEADER_TYPE_PRODUCER_TEST: canonical_type_producer_test_text.replace(
                    "CanonicalHeaderTypeProducer resolves duplicate generic names to the first ordinal",
                    "CanonicalHeaderTypeProducer rejects duplicate generic names in one binder",
                    1,
                )
            },
            "missing producer test marker",
        )
    )
    binding_verifier_text = (ROOT / BINDING_VERIFIER).read_text(encoding="utf-8")
    cases.append(
        (
            "verifier reuses canonical type producer",
            copy.deepcopy(baseline),
            {
                BINDING_VERIFIER: binding_verifier_text
                + "\nvoid forbiddenCanonicalHeaderTypeProducerReuse() { "
                + "(void)sizeof(CanonicalHeaderTypeProducer); }\n"
            },
            "independent verifier must not call the canonical type producer",
        )
    )

    canonical_definition_producer_text = (
        ROOT / CANONICAL_DEFINITION_HEADER_PRODUCER_IMPLEMENTATION
    ).read_text(encoding="utf-8")
    canonical_definition_producer_test_text = (
        ROOT / CANONICAL_DEFINITION_HEADER_PRODUCER_TEST
    ).read_text(encoding="utf-8")
    cases.append(
        (
            "canonical definition producer duplicate binder admission drift",
            copy.deepcopy(baseline),
            {
                CANONICAL_DEFINITION_HEADER_PRODUCER_TEST: canonical_definition_producer_test_text.replace(
                    "CanonicalDefinitionHeaderProducer admits duplicate unused generic names",
                    "CanonicalDefinitionHeaderProducer rejects duplicate unused generic names",
                    1,
                )
            },
            "missing definition producer test marker",
        )
    )
    cases.append(
        (
            "missing extern callable producer mapping",
            copy.deepcopy(baseline),
            {
                CANONICAL_DEFINITION_HEADER_PRODUCER_IMPLEMENTATION: (
                    canonical_definition_producer_text.replace(
                        "case ast::SyntaxKind::ExternDecl:",
                        "case ast::SyntaxKind::FunctionDecl:",
                        1,
                    )
                )
            },
            "missing callable mapping for ExternDecl",
        )
    )
    cases.append(
        (
            "callable generic depth zero frame removed",
            copy.deepcopy(baseline),
            {
                CANONICAL_DEFINITION_HEADER_PRODUCER_IMPLEMENTATION: (
                    canonical_definition_producer_text.replace(
                        "frames.add(CanonicalGenericBinderFrame{syntax.genericParameters});",
                        "frames.addAll(enclosingBinders);",
                        1,
                    )
                )
            },
            "producer must reserve the callable binder at depth zero",
        )
    )
    cases.append(
        (
            "callable inventory name check removed",
            copy.deepcopy(baseline),
            {
                CANONICAL_DEFINITION_HEADER_PRODUCER_IMPLEMENTATION: (
                    canonical_definition_producer_text.replace(
                        "tree.ident(name) != tree.ident(definition.declaredName)",
                        "false",
                        1,
                    )
                )
            },
            "producer must require inventory and header name equality",
        )
    )
    cases.append(
        (
            "callable where obligation merge removed",
            copy.deepcopy(baseline),
            {
                CANONICAL_DEFINITION_HEADER_PRODUCER_IMPLEMENTATION: (
                    canonical_definition_producer_text.replace(
                        "appendWhere(", "appendWhereMutated(", 2
                    )
                )
            },
            "producer must merge where-clause obligations",
        )
    )
    cases.append(
        (
            "verifier reuses canonical definition producer",
            copy.deepcopy(baseline),
            {
                BINDING_VERIFIER: binding_verifier_text
                + "\nvoid forbiddenCanonicalDefinitionHeaderProducerReuse() { "
                + "(void)sizeof(CanonicalDefinitionHeaderProducer); }\n"
            },
            "independent verifier must not call the canonical definition producer",
        )
    )

    canonical_impl_producer_text = (
        ROOT / CANONICAL_IMPL_HEADER_PRODUCER_IMPLEMENTATION
    ).read_text(encoding="utf-8")
    canonical_impl_producer_test_text = (
        ROOT / CANONICAL_IMPL_HEADER_PRODUCER_TEST
    ).read_text(encoding="utf-8")
    cases.append(
        (
            "canonical impl producer duplicate binder admission drift",
            copy.deepcopy(baseline),
            {
                CANONICAL_IMPL_HEADER_PRODUCER_TEST: canonical_impl_producer_test_text.replace(
                    "CanonicalImplHeaderProducer rejects generic traits and admits duplicate binder names",
                    "CanonicalImplHeaderProducer rejects generic traits and duplicate binder names",
                    1,
                )
            },
            "missing impl producer test marker",
        )
    )
    cases.append(
        (
            "missing marker impl producer mapping",
            copy.deepcopy(baseline),
            {
                CANONICAL_IMPL_HEADER_PRODUCER_IMPLEMENTATION: (
                    canonical_impl_producer_text.replace(
                        "syntax.kind == ast::SyntaxKind::MarkerImpl",
                        "syntax.kind == ast::SyntaxKind::StandaloneImplDecl",
                        1,
                    )
                )
            },
            "producer must map marker impl syntax",
        )
    )
    cases.append(
        (
            "implementation generic depth zero frame removed",
            copy.deepcopy(baseline),
            {
                CANONICAL_IMPL_HEADER_PRODUCER_IMPLEMENTATION: (
                    canonical_impl_producer_text.replace(
                        "frames.add(CanonicalGenericBinderFrame{genericParameters});",
                        "frames.addAll(enclosingBinders);",
                        1,
                    )
                )
            },
            "producer must reserve the implementation binder at depth zero",
        )
    )
    cases.append(
        (
            "safe positive marker rejection drift",
            copy.deepcopy(baseline),
            {
                CANONICAL_IMPL_HEADER_PRODUCER_IMPLEMENTATION: (
                    canonical_impl_producer_text.replace(
                        "polarity == ImplPolarity::Negative && safety == ImplSafety::Unsafe",
                        "polarity == ImplPolarity::Positive && safety == ImplSafety::Safe",
                        1,
                    )
                )
            },
            "producer must reject only the parser-invalid negative unsafe marker combination",
        )
    )
    cases.append(
        (
            "positive safe marker producer test removed",
            copy.deepcopy(baseline),
            {
                CANONICAL_IMPL_HEADER_PRODUCER_TEST: canonical_impl_producer_test_text.replace(
                    "requireHeader(safeResult).safety() == identity::ImplSafety::Safe",
                    "requireHeader(safeResult).safety() == identity::ImplSafety::Unsafe",
                    1,
                )
            },
            "missing impl producer test marker",
        )
    )
    cases.append(
        (
            "verifier reuses canonical impl producer",
            copy.deepcopy(baseline),
            {
                BINDING_VERIFIER: binding_verifier_text
                + "\nvoid forbiddenCanonicalImplHeaderProducerReuse() { "
                + "(void)sizeof(CanonicalImplHeaderProducer); }\n"
            },
            "independent verifier must not call the canonical impl producer",
        )
    )

    canonical_overload_text = (ROOT / CANONICAL_OVERLOAD_HEADER_IMPLEMENTATION).read_text(
        encoding="utf-8"
    )
    cases.append(
        (
            "missing canonical overload source registration",
            copy.deepcopy(baseline),
            {
                IDENTITY_CMAKE: identity_cmake_text.replace(
                    "  ${CMAKE_CURRENT_SOURCE_DIR}/canonical/overload-header.cc\n", ""
                )
            },
            "missing canonical overload source canonical/overload-header.cc",
        )
    )
    cases.append(
        (
            "explicit Unit callable result not normalized",
            copy.deepcopy(baseline),
            {
                CANONICAL_OVERLOAD_HEADER_IMPLEMENTATION: canonical_overload_text.replace(
                    "if (kind == PredefinedTypeKind::Unit) { return unit(); }",
                    "if (false && kind == PredefinedTypeKind::Unit) { return unit(); }",
                )
            },
            "explicit Unit result must normalize to the unit variant",
        )
    )
    cases.append(
        (
            "misordered canonical overload fields",
            copy.deepcopy(baseline),
            {
                CANONICAL_OVERLOAD_HEADER_IMPLEMENTATION: canonical_overload_text.replace(
                    "  encodeSequence(encoder, impl->obligations.asPtr());\n"
                    "  encodeSequence(encoder, impl->parameters.asPtr());",
                    "  encodeSequence(encoder, impl->parameters.asPtr());\n"
                    "  encodeSequence(encoder, impl->obligations.asPtr());",
                )
            },
            "canonical overload nine-field codec must preserve canonical field order",
        )
    )
    cases.append(
        (
            "length-wrapped canonical overload record",
            copy.deepcopy(baseline),
            {
                CANONICAL_OVERLOAD_HEADER_IMPLEMENTATION: canonical_overload_text.replace(
                    "  encoder.encodeUint8(static_cast<uint8_t>(impl->callableKind));",
                    "  encoder.encodeUint8(static_cast<uint8_t>(impl->callableKind));\n"
                    "  encoder.encodeByteString(zc::ArrayPtr<const uint8_t>());",
                )
            },
            "canonical overload records must encode inline",
        )
    )
    cases.append(
        (
            "missing constructor overload admission",
            copy.deepcopy(baseline),
            {
                CANONICAL_OVERLOAD_HEADER_IMPLEMENTATION: canonical_overload_text.replace(
                    "} else if (receiver != zc::none || externalAbi != zc::none || "
                    "!constructorResult) {",
                    "} else if (receiver != zc::none || externalAbi != zc::none) {",
                )
            },
            "missing overload admission marker",
        )
    )
    canonical_overload_test_text = (ROOT / CANONICAL_OVERLOAD_HEADER_TEST).read_text(
        encoding="utf-8"
    )
    cases.append(
        (
            "missing explicit Unit byte-equivalence regression",
            copy.deepcopy(baseline),
            {
                CANONICAL_OVERLOAD_HEADER_TEST: canonical_overload_test_text.replace(
                    "ZC_EXPECT(explicitUnit.encode().asPtr() == unit.encode().asPtr());",
                    "ZC_EXPECT(explicitUnit.encode().asPtr() != unit.encode().asPtr());",
                )
            },
            "missing canonical overload test marker",
        )
    )

    overload_digest_text = (ROOT / OVERLOAD_HEADER_DIGEST_IMPLEMENTATION).read_text(
        encoding="utf-8"
    )
    cases.append(
        (
            "missing overload digest source registration",
            copy.deepcopy(baseline),
            {
                IDENTITY_CMAKE: identity_cmake_text.replace(
                    "  ${CMAKE_CURRENT_SOURCE_DIR}/crypto/overload-header-digest.cc\n", ""
                )
            },
            "missing overload digest source crypto/overload-header-digest.cc",
        )
    )
    cases.append(
        (
            "overload digest domain drift",
            copy.deepcopy(baseline),
            {
                OVERLOAD_HEADER_DIGEST_IMPLEMENTATION: overload_digest_text.replace(
                    '"zom.overload-header"_zc', '"zom.overload-header.mutated"_zc'
                )
            },
            "invalid overload header digest domain",
        )
    )
    cases.append(
        (
            "overload digest separator drift",
            copy.deepcopy(baseline),
            {
                OVERLOAD_HEADER_DIGEST_IMPLEMENTATION: overload_digest_text.replace(
                    "preimage.add(0x00);", "preimage.add(0xff);"
                )
            },
            "overload header digest preimage is missing preimage.add(0x00);",
        )
    )
    cases.append(
        (
            "length-wrapped overload digest",
            copy.deepcopy(baseline),
            {
                OVERLOAD_HEADER_DIGEST_IMPLEMENTATION: overload_digest_text.replace(
                    "encoder.encodeDigest(digestValue);",
                    "encoder.encodeByteString(digestValue.bytes());",
                )
            },
            "overload digest must encode as raw 32 bytes",
        )
    )
    cases.append(
        (
            "digest-only overload record comparison",
            copy.deepcopy(baseline),
            {
                OVERLOAD_HEADER_DIGEST_IMPLEMENTATION: overload_digest_text.replace(
                    "  const auto left = impl->header.encode();\n"
                    "  const auto right = other.impl->header.encode();\n"
                    "  return left.asPtr() == right.asPtr();",
                    "  return impl->digest == other.impl->digest;",
                )
            },
            "complete overload record comparison is missing impl->header.encode();",
        )
    )

    canonical_impl_text = (ROOT / CANONICAL_IMPL_HEADER_IMPLEMENTATION).read_text(
        encoding="utf-8"
    )
    canonical_impl_header_text = (ROOT / CANONICAL_IMPL_HEADER).read_text(encoding="utf-8")
    canonical_impl_test_text = (ROOT / CANONICAL_IMPL_HEADER_TEST).read_text(encoding="utf-8")
    cases.append(
        (
            "missing canonical impl source registration",
            copy.deepcopy(baseline),
            {
                IDENTITY_CMAKE: identity_cmake_text.replace(
                    "  ${CMAKE_CURRENT_SOURCE_DIR}/canonical/impl-header.cc\n", ""
                )
            },
            "missing canonical impl source canonical/impl-header.cc",
        )
    )
    cases.append(
        (
            "canonical impl polarity tag drift",
            copy.deepcopy(baseline),
            {
                CANONICAL_IMPL_HEADER: canonical_impl_header_text.replace(
                    "Positive = 0x01", "Positive = 0x03"
                )
            },
            "ImplPolarity must retain its exact RFC 0018 tags",
        )
    )
    cases.append(
        (
            "generic trait root admitted",
            copy.deepcopy(baseline),
            {
                CANONICAL_IMPL_HEADER_IMPLEMENTATION: canonical_impl_text.replace(
                    "rootKind != CanonicalNameRootKind::Absolute && "
                    "rootKind != CanonicalNameRootKind::Relative",
                    "rootKind != CanonicalNameRootKind::Absolute && "
                    "rootKind != CanonicalNameRootKind::Relative && "
                    "rootKind != CanonicalNameRootKind::Generic",
                )
            },
            "canonical trait root must admit only absolute and relative names",
        )
    )
    cases.append(
        (
            "misordered canonical trait fields",
            copy.deepcopy(baseline),
            {
                CANONICAL_IMPL_HEADER_IMPLEMENTATION: canonical_impl_text.replace(
                    "  impl->name.encode(encoder);\n"
                    "  encoder.encodeSequenceSize(impl->arguments.size());",
                    "  encoder.encodeSequenceSize(impl->arguments.size());\n"
                    "  impl->name.encode(encoder);",
                )
            },
            "canonical trait field codec must preserve canonical field order",
        )
    )
    cases.append(
        (
            "resolved trait identity field",
            copy.deepcopy(baseline),
            {
                CANONICAL_IMPL_HEADER_IMPLEMENTATION: canonical_impl_text.replace(
                    "  CanonicalNameReference name;",
                    "  CanonicalNameReference name;\n  DefinitionKey resolved;",
                )
            },
            "canonical trait reference must contain exactly name and ordered arguments",
        )
    )
    cases.append(
        (
            "misordered canonical impl header fields",
            copy.deepcopy(baseline),
            {
                CANONICAL_IMPL_HEADER_IMPLEMENTATION: canonical_impl_text.replace(
                    "  encoder.encodeUint8(static_cast<uint8_t>(impl->polarity));\n"
                    "  encoder.encodeUint8(static_cast<uint8_t>(impl->safety));",
                    "  encoder.encodeUint8(static_cast<uint8_t>(impl->safety));\n"
                    "  encoder.encodeUint8(static_cast<uint8_t>(impl->polarity));",
                    1,
                )
            },
            "canonical impl header field codec must preserve canonical field order",
        )
    )
    cases.append(
        (
            "impl obligation canonicalization removed",
            copy.deepcopy(baseline),
            {
                CANONICAL_IMPL_HEADER_IMPLEMENTATION: canonical_impl_text.replace(
                    "obligations = sortUnique(zc::mv(obligations));",
                    "obligations = zc::mv(obligations);",
                    1,
                )
            },
            "impl header admission is missing obligations = sortUnique",
        )
    )
    cases.append(
        (
            "canonical impl header fixed vector drift",
            copy.deepcopy(baseline),
            {
                CANONICAL_IMPL_HEADER_TEST: canonical_impl_test_text.replace(
                    '"0000000000000000010102000000000000000100000000000000055472616974"',
                    '"mutated"',
                    1,
                )
            },
            "missing canonical trait test marker",
        )
    )

    header_schema_text = (ROOT / HEADER_SYNTAX_SCHEMA).read_text(encoding="utf-8")
    cases.append(
        (
            "canonical header tag drift",
            copy.deepcopy(baseline),
            {
                HEADER_SYNTAX_SCHEMA: header_schema_text.replace(
                    "- {name: Function, tag: 0x01}",
                    "- {name: Function, tag: 0x04}",
                    1,
                )
            },
            "canonical header schema generation failed",
        )
    )
    cases.append(
        (
            "canonical header field drift",
            copy.deepcopy(baseline),
            {
                HEADER_SYNTAX_SCHEMA: header_schema_text.replace(
                    "- {name: callableKind, type: CallableHeaderKind}",
                    "- {name: sourceKind, type: CallableHeaderKind}",
                    1,
                )
            },
            "canonical header schema generation failed",
        )
    )
    cases.append(
        (
            "canonical header AST id",
            copy.deepcopy(baseline),
            {
                HEADER_SYNTAX_SCHEMA: header_schema_text.replace(
                    "- {name: arguments, type: Sequence<CanonicalHeaderTypeSyntax>}",
                    "- {name: arguments, type: Sequence<CanonicalHeaderTypeSyntax>}\n"
                    "          - {name: syntax, type: NodeId}",
                    1,
                )
            },
            "AST or provenance value is forbidden",
        )
    )

    header_definition_text = (ROOT / HEADER_SYNTAX_DEFINITION).read_text(encoding="utf-8")
    cases.append(
        (
            "canonical header generated drift",
            copy.deepcopy(baseline),
            {HEADER_SYNTAX_DEFINITION: header_definition_text + "// drift\n"},
            "generated canonical header definition is stale",
        )
    )

    for name, manifest, overrides, expected in cases:
        errors = analyze(manifest, overrides)
        if not any(expected in error for error in errors):
            print(f"error: negative fixture did not fail: {name}", file=sys.stderr)
            return 1
        print(f"negative fixture passed: {name}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Check semantic identity architecture")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true", help="check the live repository")
    mode.add_argument("--self-test", action="store_true", help="run negative fixtures")
    args = parser.parse_args()
    return run_self_test() if args.self_test else run_check()


if __name__ == "__main__":
    raise SystemExit(main())
