#!/usr/bin/env python3

import argparse
import copy
import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCHEMA = Path("products/zomlang/compiler/ast/schema.yml")
MANIFEST = Path("products/zomlang/compiler/identity/definition-producers.json")
DEFINITION_KEY = Path("products/zomlang/compiler/identity/definition-key.h")
INVENTORY = Path("products/zomlang/compiler/binder/definition-inventory.cc")
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


def read_text(path: Path, overrides: dict[Path, str]) -> str:
    if path in overrides:
        return overrides[path]
    return (ROOT / path).read_text(encoding="utf-8")


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
    if manifest.get("version") != 1:
        errors.append(f"{MANIFEST}: version must be 1")
    for key in (
        "producers",
        "no_identity",
        "expansion_producers",
        "legacy_symbol_id_allowlist",
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
    anonymous_roles = enum_members(definition_text, "AnonymousDefinitionRole")

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
    maker = re.compile(
        r"\bmake(" + "|".join(re.escape(name) for name in sorted(producers)) + r")\s*\("
    )
    for path in sorted(COMPILER_ROOT.rglob("*.cc")):
        relative_path = path.relative_to(ROOT)
        if relative_path.parent == PARSER_ROOT:
            continue
        text = read_text(relative_path, overrides)
        for match in maker.finditer(text):
            errors.append(
                f"{relative_path}: post-parse semantic producer make{match.group(1)} is forbidden"
            )


def matching_files(pattern: re.Pattern[str], overrides: dict[Path, str]) -> set[str]:
    matches: set[str] = set()
    for suffix in ("*.h", "*.cc"):
        for path in COMPILER_ROOT.rglob(suffix):
            relative_path = path.relative_to(ROOT)
            if pattern.search(read_text(relative_path, overrides)):
                matches.add(str(relative_path))
    return matches


def check_phase_local_allowlists(
    manifest: dict[str, object], overrides: dict[Path, str], errors: list[str]
) -> None:
    checks = (
        (
            "legacy_symbol_id_allowlist",
            re.compile(r"\bSymbolId\b"),
            "legacy SymbolId surface",
        ),
        (
            "pointer_identity_allowlist",
            POINTER_IDENTITY_PATTERN,
            "pointer-derived identity surface",
        ),
    )
    for key, pattern, description in checks:
        configured = manifest.get(key, [])
        if not isinstance(configured, list) or not all(isinstance(item, str) for item in configured):
            errors.append(f"{MANIFEST}: {key} must be a string list")
            continue
        expected = set(configured)
        actual = matching_files(pattern, overrides)
        for path in sorted(actual - expected):
            errors.append(f"{path}: unallowlisted {description}")
        for path in sorted(expected - actual):
            errors.append(f"{MANIFEST}: stale {description} allowlist entry {path}")


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
    check_phase_local_allowlists(manifest, active_overrides, errors)
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

    missing_allowlist = copy.deepcopy(baseline)
    missing_allowlist["legacy_symbol_id_allowlist"].remove(  # type: ignore[union-attr]
        "products/zomlang/compiler/ast/tree.h"
    )
    cases.append(
        ("unallowlisted legacy identity", missing_allowlist, {}, "unallowlisted legacy SymbolId")
    )

    for name, manifest, overrides, expected in cases:
        errors = analyze(manifest, overrides)
        if not any(expected in error for error in errors):
            print(f"error: negative fixture did not fail: {name}", file=sys.stderr)
            return 1
        print(f"negative fixture passed: {name}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Check RFC 0011 semantic identity architecture")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true", help="check the live repository")
    mode.add_argument("--self-test", action="store_true", help="run negative fixtures")
    args = parser.parse_args()
    return run_self_test() if args.self_test else run_check()


if __name__ == "__main__":
    raise SystemExit(main())
