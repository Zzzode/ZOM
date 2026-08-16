#!/usr/bin/env python3
"""Validate the current lexical, grammar, expression, AST, and parser contract."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
LEXICAL = Path("docs/spec/chapters/02-lexical-structure.md")
GRAMMAR = Path("docs/spec/chapters/17-grammar-reference.md")
EXPRESSIONS = Path("docs/spec/chapters/04-expressions.md")
LEXER = Path("docs/spec/ZomLexer.g4")
KINDS = Path("products/zomlang/compiler/ast/kinds.h")
PARSER_HELPERS = Path("products/zomlang/compiler/parser/parser-helpers.cc")
EXPRESSION_PARSER = Path("products/zomlang/compiler/parser/syntax/expression-parser.cc")
REQUIRED = (LEXICAL, GRAMMAR, EXPRESSIONS, LEXER, KINDS, PARSER_HELPERS, EXPRESSION_PARSER)


def read_files(root: Path) -> dict[Path, str]:
    return {path: (root / path).read_text(encoding="utf-8") for path in REQUIRED}


def section(text: str, start: str, end: str) -> str:
    match = re.search(rf"{re.escape(start)}(.*?){re.escape(end)}", text, re.DOTALL)
    return match.group(1) if match else ""


def words_in_fences(text: str) -> set[str]:
    words: set[str] = set()
    for block in re.findall(r"```(?:[A-Za-z0-9_+-]+)?\n(.*?)```", text, re.DOTALL):
        words.update(re.findall(r"\b[a-z][a-z0-9_]*\b", block))
    return words


def lexical_keywords(text: str) -> set[str]:
    return words_in_fences(section(text, "## Keywords", "## Literals"))


def ast_keywords(text: str) -> set[str]:
    return set(re.findall(r"^\s*\w+Keyword,\s*//\s*([a-z][a-z0-9_]*)\s*$", text, re.MULTILINE))


def lexer_keywords(text: str) -> set[str]:
    return set(
        re.findall(r"^\s*[A-Z][A-Z0-9_]*\s*:\s*'([a-z][a-z0-9_]*)'", text, re.MULTILINE)
    )


def quoted_words(text: str, production: str) -> set[str]:
    match = re.search(rf"^{production}\s*::=\s*(.*)$", text, re.MULTILINE)
    return set(re.findall(r"'([a-z][a-z0-9_]*)'", match.group(1))) if match else set()


def parser_modifier_words(text: str, function: str, kinds: str) -> set[str]:
    match = re.search(rf"bool {function}\(.*?\n\}}", text, re.DOTALL)
    if match is None:
        return set()
    names = set(re.findall(r"SyntaxKind::(\w+Keyword)", match.group(0)))
    mapping = dict(re.findall(r"^\s*(\w+Keyword),\s*//\s*([a-z][a-z0-9_]*)\s*$", kinds, re.MULTILINE))
    return {mapping[name] for name in names if name in mapping}


def quoted_symbols(text: str, production: str) -> set[str]:
    match = re.search(rf"^{production}\s*::=\s*(.*)$", text, re.MULTILINE)
    return set(re.findall(r"'([^']+)'", match.group(1))) if match else set()


def expression_postfix_symbols(text: str) -> set[str]:
    match = re.search(r"^\| 2 \| Postfix \| (.*?) \|", text, re.MULTILINE)
    return set(re.findall(r"`([^`]+)`", match.group(1))) if match else set()


def syntax_kind_for_symbol(text: str, symbol: str) -> str | None:
    escaped = re.escape(symbol)
    match = re.search(rf"^\s*(\w+),\s*//\s*{escaped}\s*$", text, re.MULTILINE)
    return match.group(1) if match else None


def postfix_parser_symbols(kinds: str, helpers: str, expected: set[str]) -> set[str]:
    match = re.search(r"bool isPostfixOperator\(.*?\n\}", helpers, re.DOTALL)
    body = match.group(0) if match else ""
    found: set[str] = set()
    for symbol in expected:
        kind = syntax_kind_for_symbol(kinds, symbol)
        if kind is not None and f"SyntaxKind::{kind}" in body:
            found.add(symbol)
    return found


def differences(label: str, expected: set[str], actual: set[str], errors: list[str]) -> None:
    missing = sorted(expected - actual)
    unexpected = sorted(actual - expected)
    if missing:
        errors.append(f"{label}: missing {', '.join(missing)}")
    if unexpected:
        errors.append(f"{label}: unexpected {', '.join(unexpected)}")


def check(values: dict[Path, str]) -> list[str]:
    errors: list[str] = []
    lexical = lexical_keywords(values[LEXICAL])
    ast = ast_keywords(values[KINDS])
    lexer = lexer_keywords(values[LEXER])
    differences("lexical keywords versus AST keywords", lexical, ast, errors)
    differences("lexical keywords versus lexer keywords", lexical, lexer, errors)

    for production, function in (
        ("VisibilityModifier", "isVisibilityModifier"),
        ("BehaviorModifier", "isBehaviorModifier"),
    ):
        specified = quoted_words(values[GRAMMAR], production)
        parsed = parser_modifier_words(values[PARSER_HELPERS], function, values[KINDS])
        differences(f"{production} grammar versus parser", specified, parsed, errors)

    grammar_postfix = quoted_symbols(values[GRAMMAR], "PostfixSuffix")
    expression_postfix = expression_postfix_symbols(values[EXPRESSIONS])
    parser_postfix = postfix_parser_symbols(values[KINDS], values[PARSER_HELPERS], grammar_postfix)
    differences("PostfixSuffix grammar versus expression chapter", grammar_postfix, expression_postfix, errors)
    differences("PostfixSuffix grammar versus parser", grammar_postfix, parser_postfix, errors)

    for source, marker in (
        (EXPRESSION_PARSER, "parsePostfixExpressionAt"),
        (PARSER_HELPERS, "binaryPrecedence"),
        (GRAMMAR, "AdditiveExpression ::="),
        (EXPRESSIONS, "## Operator Precedence"),
    ):
        if marker not in values[source]:
            errors.append(f"{source}: missing required alignment anchor {marker}")
    return errors


def report(errors: list[str]) -> str:
    return json.dumps(
        {"schema": "zom.spec-alignment", "result": "pass" if not errors else "fail", "errors": errors},
        indent=2,
        sort_keys=True,
    ) + "\n"


def self_test() -> int:
    values = read_files(ROOT)
    if check(values):
        print("spec-alignment baseline failed")
        return 1
    mutation = dict(values)
    mutation[LEXER] = mutation[LEXER].replace("FUN      : 'fun';", "FUN      : 'function';", 1)
    if not any("lexer keywords" in error for error in check(mutation)):
        print("spec-alignment self-test escaped lexer keyword mutation")
        return 1
    mutation = dict(values)
    mutation[GRAMMAR] = mutation[GRAMMAR].replace("PostfixSuffix ::= '?!' | '!!' | '++' | '--'", "PostfixSuffix ::= '?!' | '!!' | '++'", 1)
    if not any("PostfixSuffix" in error for error in check(mutation)):
        print("spec-alignment self-test escaped postfix mutation")
        return 1
    mutation = dict(values)
    mutation[PARSER_HELPERS] = mutation[PARSER_HELPERS].replace("case ast::SyntaxKind::PublicKeyword:", "case ast::SyntaxKind::PrivateKeyword:", 1)
    if not any("VisibilityModifier" in error for error in check(mutation)):
        print("spec-alignment self-test escaped modifier mutation")
        return 1
    print("spec-alignment self-test passed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--self-test", action="store_true")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    errors = check(read_files(ROOT))
    if args.report is not None:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(report(errors), encoding="utf-8")
    if errors:
        for error in errors:
            print(f"error: {error}")
        return 1
    print("spec-alignment check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
