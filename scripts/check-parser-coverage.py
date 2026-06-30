#!/usr/bin/env python3

import re
import sys
from pathlib import Path

import yaml


ROOT = Path(__file__).resolve().parents[1]
GRAMMAR = ROOT / "docs" / "spec" / "chapters" / "17-grammar-reference.md"
PARSER = ROOT / "products" / "zomlang" / "compiler" / "parser" / "parser.cc"
COVERAGE = ROOT / "products" / "zomlang" / "compiler" / "parser" / "parser-coverage.yml"
SCHEMA = ROOT / "products" / "zomlang" / "compiler" / "ast" / "schema.yml"

ALLOWED_STATUS = {"direct", "inlined", "lexical", "rejected"}


def rel(path: Path) -> str:
    return str(path.relative_to(ROOT))


def extract_productions() -> dict[str, str]:
    productions: dict[str, str] = {}
    section = ""
    in_ebnf = False
    pending_name: str | None = None

    for line in GRAMMAR.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if stripped == "### Lexical Grammar":
            section = "lexical"
            continue
        if stripped == "### Syntactic Grammar":
            section = "syntactic"
            continue
        if stripped == "```ebnf":
            in_ebnf = True
            continue
        if stripped == "```" and in_ebnf:
            in_ebnf = False
            pending_name = None
            continue
        if not in_ebnf or not section:
            continue

        match = re.match(r"^([A-Za-z][A-Za-z0-9_]*)\s*::=", stripped)
        if match:
            productions.setdefault(match.group(1), section)
            pending_name = None
            continue

        if pending_name and stripped.startswith("::="):
            productions.setdefault(pending_name, section)
            pending_name = None
            continue

        pending = re.match(r"^([A-Za-z][A-Za-z0-9_]*)\s*$", stripped)
        pending_name = pending.group(1) if pending else None

    return productions


def load_coverage() -> dict[str, object]:
    if not COVERAGE.exists():
        fail(f"{rel(COVERAGE)} does not exist")
        return {}

    with COVERAGE.open("r", encoding="utf-8") as handle:
        data = yaml.safe_load(handle)
    if not isinstance(data, dict):
        fail(f"{rel(COVERAGE)} must contain a YAML mapping")
        return {}

    productions = data.get("productions")
    if not isinstance(productions, dict):
        fail(f"{rel(COVERAGE)} must define a 'productions' mapping")
        return {}
    return productions


def parser_functions() -> set[str]:
    text = PARSER.read_text(encoding="utf-8")
    functions = set(re.findall(r"\b(parse[A-Za-z0-9_]*)\s*\(", text))
    if "Parser::parse(" in text:
        functions.add("parse")
    if re.search(r"\bbuildTree\s*\(", text):
        functions.add("buildTree")
    return functions


def ast_kinds() -> set[str]:
    with SCHEMA.open("r", encoding="utf-8") as handle:
        schema = yaml.safe_load(handle)
    variants = schema.get("variants", []) if isinstance(schema, dict) else []
    return {
        str(variant.get("name"))
        for variant in variants
        if isinstance(variant, dict) and variant.get("name")
    }


def parser_constructed_kinds() -> set[str]:
    text = PARSER.read_text(encoding="utf-8")
    return set(re.findall(r"SyntaxKind::([A-Za-z][A-Za-z0-9_]*)", text))


def as_string_list(value: object, field: str, production: str) -> list[str]:
    if value is None:
        return []
    if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
        fail(f"{production}: '{field}' must be a list of strings")
        return []
    return value


def validate_entry(
    name: str,
    entry: object,
    grammar_kind: str,
    grammar_productions: set[str],
    functions: set[str],
    schema_kinds: set[str],
    constructed_kinds: set[str],
) -> None:
    if not isinstance(entry, dict):
        fail(f"{name}: coverage entry must be a mapping")
        return

    status = entry.get("status")
    if status not in ALLOWED_STATUS:
        fail(f"{name}: status must be one of {sorted(ALLOWED_STATUS)}")
        return

    if grammar_kind == "lexical" and status != "lexical":
        fail(f"{name}: lexical grammar production must use status 'lexical'")
    if grammar_kind == "syntactic" and status == "lexical":
        fail(f"{name}: syntactic grammar production cannot use status 'lexical'")
    if status == "rejected":
        fail(f"{name}: rejected production still exists in {rel(GRAMMAR)}")

    parser = entry.get("parser")
    if status in {"direct", "inlined"}:
        if not isinstance(parser, str) or not parser:
            fail(f"{name}: {status} coverage entry must name a parser function")
        elif parser not in functions:
            fail(f"{name}: parser function '{parser}' is not present in {rel(PARSER)}")

    parent = entry.get("parent")
    if status == "inlined":
        if not isinstance(parent, str) or not parent:
            fail(f"{name}: inlined coverage entry must name a parent production")
        elif parent not in grammar_productions:
            fail(f"{name}: parent production '{parent}' is not present in {rel(GRAMMAR)}")

    if status == "direct" and parent is not None:
        fail(f"{name}: direct coverage entry must not set parent")

    for test_path in as_string_list(entry.get("tests"), "tests", name):
        path = ROOT / test_path
        if not path.exists():
            fail(f"{name}: mapped test path does not exist: {test_path}")

    for kind in as_string_list(entry.get("ast"), "ast", name):
        if kind not in schema_kinds:
            fail(f"{name}: mapped AST kind does not exist in {rel(SCHEMA)}: {kind}")
        elif kind not in constructed_kinds:
            fail(f"{name}: mapped AST kind is not constructed by parser.cc: {kind}")


errors: list[str] = []


def fail(message: str) -> None:
    errors.append(message)


def main() -> int:
    grammar = extract_productions()
    coverage = load_coverage()
    functions = parser_functions()
    schema_kinds = ast_kinds()
    constructed_kinds = parser_constructed_kinds()

    grammar_names = set(grammar)
    coverage_names = set(coverage)

    for name in sorted(grammar_names - coverage_names):
        fail(f"{name}: missing parser coverage entry")
    for name in sorted(coverage_names - grammar_names):
        fail(f"{name}: coverage entry does not match a grammar production")

    for name in sorted(grammar_names & coverage_names):
        validate_entry(
            name,
            coverage[name],
            grammar[name],
            grammar_names,
            functions,
            schema_kinds,
            constructed_kinds,
        )

    if errors:
        print("Parser coverage check failed.", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    syntactic = sum(1 for kind in grammar.values() if kind == "syntactic")
    lexical = sum(1 for kind in grammar.values() if kind == "lexical")
    print(
        "Parser coverage check passed "
        f"({syntactic} syntactic productions, {lexical} lexical productions)."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
