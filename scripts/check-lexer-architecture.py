#!/usr/bin/env python3

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
LEXER_DIR = ROOT / "products" / "zomlang" / "compiler" / "lexer"
PARSER_DIR = ROOT / "products" / "zomlang" / "compiler" / "parser"
LEXER_TEST_DIR = ROOT / "products" / "zomlang" / "tests" / "unittests" / "compiler" / "lexer"
CONFORMANCE_CORPUS = ROOT / "products" / "zomlang" / "tests" / "conformance" / "corpus"
CONFORMANCE_GRAMMAR = (
    ROOT / "products" / "zomlang" / "tests" / "conformance" / "expectations" / "grammar"
)
CONFORMANCE_AST = (
    ROOT / "products" / "zomlang" / "tests" / "conformance" / "expectations" / "ast"
)
DESIGN_DIR = ROOT / "docs" / "design"
LEXICAL_SPEC = ROOT / "docs" / "spec" / "chapters" / "02-lexical-structure.md"
ZOM_LEXER_G4 = ROOT / "docs" / "spec" / "ZomLexer.g4"
KINDS_H = ROOT / "products" / "zomlang" / "compiler" / "ast" / "kinds.h"
TOKEN_CC = LEXER_DIR / "token.cc"
UTILS_CC = LEXER_DIR / "utils.cc"
LEXER_INVENTORY_TEST = LEXER_TEST_DIR / "lexer-inventory-test.cc"
UNICODE_H = LEXER_DIR / "unicode-data.h"
UNICODE_CC = LEXER_DIR / "unicode-data.cc"
GENERATOR = ROOT / "scripts" / "codegen" / "gen_unicode_data.py"
EXPECTED_UCD_VERSION = "15.1.0"
EXPECTED_UCD_SOURCE = (
    "https://www.unicode.org/Public/15.1.0/ucd/DerivedCoreProperties.txt"
)
EXPECTED_START_RANGES = 660
EXPECTED_PART_RANGES = 769
BANNED_LEXER_APIS = [
    "LexerState",
    "restoreState",
    "getCurrentState",
    "reScanGreaterToken",
    "reScanTemplateToken",
]
PUBLIC_LEXER_API_BANNED_TERMS = BANNED_LEXER_APIS + [
    "LexerMode",
    "Snapshot",
    "snapshot",
]
PARSER_RAW_SOURCE_ACCESS_TERMS = [
    "bufferStart",
    "bufferEnd",
    "curPtr",
    "fullStartPtr",
    "getEntireTextForBuffer",
    "tokenStartPtr",
]
TEMPLATE_MODE_IMPLEMENTATION_MARKERS = [
    "templateSubstitutionBraceDepths",
    "beginTemplateSubstitution",
    "finishTemplateSpan",
    "inTemplateSubstitution",
]
TEMPLATE_MODE_TEST_MARKERS = [
    "LexerLiteralTest.TemplateLiterals",
    "TemplateSubstitutionBraceDepth",
]
DESIGN_DOC_MARKERS = {
    DESIGN_DIR / "architecture.md": [
        "Lexer::lex(Token&)",
        "Lazy TokenStream",
        "TokenCursor",
        "zc::none",
    ],
    DESIGN_DIR / "compiler-contracts.md": [
        "Lexer::lex(Token&)",
        "lazy retained `TokenStream`",
        "TokenCursor",
        "Parser::parse()` returns `zc::none`",
    ],
}
DESIGN_DOC_BANNED_TERMS = [
    "Own<TokenStream>",
    "Token[] with SourceLoc",
    "Immutable post-lex",
    "pre-lexing the whole file",
    "lexAll",
]
EXPECTED_DYNAMIC_G4_RULES = {
    "IDENTIFIER",
    "BIGINT_LITERAL",
    "DECIMAL_LITERAL",
    "BINARY_LITERAL",
    "OCTAL_LITERAL",
    "HEX_LITERAL",
    "DOUBLE_STRING_LITERAL",
    "CHAR_LITERAL",
    "NO_SUBSTITUTION_TEMPLATE_LITERAL",
    "TEMPLATE_HEAD",
    "TEMPLATE_MIDDLE",
    "TEMPLATE_TAIL",
}
EXPECTED_DYNAMIC_LEXER_KIND_MARKERS = {
    "Identifier": ["return ast::SyntaxKind::Identifier;"],
    "StringLiteral": ["formToken(ast::SyntaxKind::StringLiteral"],
    "IntegerLiteral": ["ast::SyntaxKind::IntegerLiteral"],
    "BigIntLiteralToken": ["ast::SyntaxKind::BigIntLiteralToken"],
    "FloatLiteral": ["ast::SyntaxKind::FloatLiteral"],
    "CharacterLiteral": ["ast::SyntaxKind::CharacterLiteral"],
    "NoSubstitutionTemplateLiteral": ["ast::SyntaxKind::NoSubstitutionTemplateLiteral"],
    "TemplateHead": ["ast::SyntaxKind::TemplateHead"],
    "TemplateMiddle": ["ast::SyntaxKind::TemplateMiddle"],
    "TemplateTail": ["ast::SyntaxKind::TemplateTail"],
    "Unknown": ["formToken(ast::SyntaxKind::Unknown"],
    "EndOfFile": ["formToken(ast::SyntaxKind::EndOfFile"],
}
LEXICAL_GRAMMAR_EXCEPTIONS = {
    Path("identifiers/reserved-words"),
}


errors: list[str] = []


def rel(path: Path) -> str:
    return str(path.relative_to(ROOT))


def fail(message: str) -> None:
    errors.append(message)


def range_count(text: str, name: str) -> int:
    match = re.search(rf"static constexpr UnicodeRange {name}\[\] = \{{(.*?)\n\}};", text, re.S)
    if match is None:
        fail(f"{rel(UNICODE_CC)} is missing {name}")
        return 0
    return len(re.findall(r"\{0x[0-9A-Fa-f]+, 0x[0-9A-Fa-f]+\}", match.group(1)))


def require_contains(path: Path, text: str, needle: str) -> None:
    if needle not in text:
        fail(f"{rel(path)} is missing required lexer architecture marker: {needle}")


def decode_cpp_string_literal(text: str) -> str:
    result: list[str] = []
    index = 0
    while index < len(text):
        if text[index] != "\\":
            result.append(text[index])
            index += 1
            continue

        index += 1
        if index >= len(text):
            result.append("\\")
            break

        escaped = text[index]
        index += 1
        result.append(
            {
                "0": "\0",
                "b": "\b",
                "f": "\f",
                "n": "\n",
                "r": "\r",
                "t": "\t",
                "v": "\v",
                "\\": "\\",
                '"': '"',
                "'": "'",
            }.get(escaped, escaped)
        )
    return "".join(result)


def static_token_spellings(text: str) -> dict[str, str]:
    spellings: dict[str, str] = {}
    for match in re.finditer(
        r"case ast::SyntaxKind::([A-Za-z][A-Za-z0-9_]*):\s*"
        r"return\s+((?:\"(?:\\.|[^\"])*\"\s*)+)_zc;",
        text,
        re.S,
    ):
        kind = match.group(1)
        spelling_parts = re.findall(r'"((?:\\.|[^"])*)"', match.group(2))
        spellings[kind] = "".join(decode_cpp_string_literal(part) for part in spelling_parts)
    return spellings


def inventory_test_token_kinds(text: str) -> set[str]:
    return {
        match.group(1)
        for match in re.finditer(r"\bast::SyntaxKind::([A-Za-z][A-Za-z0-9_]*)\b", text)
    }


def path_stems(root: Path, suffix: str) -> set[Path]:
    return {path.relative_to(root).with_suffix("") for path in root.rglob(f"*{suffix}")}


def parser_source_paths() -> list[Path]:
    return sorted(path for glob in ("*.cc", "*.h") for path in PARSER_DIR.glob(glob))


def lexical_spec_keywords(text: str) -> set[str]:
    section = text[text.index("## Keywords") : text.index("## Literals")]
    keywords: set[str] = set()
    for block in re.findall(r"```\n(.*?)```", section, re.S):
        keywords.update(re.findall(r"\b[a-z][a-z0-9_]*\b", block))
    return keywords


def lexical_spec_symbols(text: str) -> set[str]:
    section = text[text.index("## Punctuators and Operators") :]
    symbols: set[str] = set()
    for block in re.findall(r"```\n(.*?)```", section, re.S):
        for line in block.splitlines():
            stripped = line.strip()
            if not stripped:
                continue
            left_side = re.split(r"\s{2,}", stripped, maxsplit=1)[0]
            symbols.update(left_side.split())
    return symbols


def check_equal_set(label: str, expected: set[str], actual: set[str], actual_path: Path) -> None:
    missing = sorted(expected - actual)
    extra = sorted(actual - expected)
    if missing:
        fail(f"{rel(actual_path)} is missing {label}: {', '.join(missing)}")
    if extra:
        fail(f"{rel(actual_path)} contains unsupported {label}: {', '.join(extra)}")


def check_token_inventory_alignment() -> None:
    lexical_spec = LEXICAL_SPEC.read_text(encoding="utf-8")
    lexer_g4 = ZOM_LEXER_G4.read_text(encoding="utf-8")
    kinds_h = KINDS_H.read_text(encoding="utf-8")
    token_cc = TOKEN_CC.read_text(encoding="utf-8")
    utils_cc = UTILS_CC.read_text(encoding="utf-8")
    lexer_cc = (LEXER_DIR / "lexer.cc").read_text(encoding="utf-8")
    inventory_test = LEXER_INVENTORY_TEST.read_text(encoding="utf-8")

    spec_keywords = lexical_spec_keywords(lexical_spec)
    spec_symbols = lexical_spec_symbols(lexical_spec)

    kind_keywords = {
        match.group(1).strip()
        for match in re.finditer(
            r"\b[A-Za-z][A-Za-z0-9_]*Keyword,\s*//\s*([a-z][a-z0-9_]*)",
            kinds_h,
        )
    }
    utils_keywords = {
        match.group(1)
        for match in re.finditer(
            r'if \(text == "([a-z][a-z0-9_]*)"_zcb\) return '
            r"ast::SyntaxKind::[A-Za-z][A-Za-z0-9_]*Keyword;",
            utils_cc,
        )
    }
    g4_keywords = {
        match.group(2)
        for match in re.finditer(
            r"^([A-Z][A-Z0-9_]*)\s*:\s*'([a-z][a-z0-9_]*)'\s*;",
            lexer_g4,
            re.M,
        )
    }

    static_spellings = static_token_spellings(token_cc)
    static_keywords = {
        spelling for kind, spelling in static_spellings.items() if kind.endswith("Keyword")
    }
    static_symbols = {
        spelling for kind, spelling in static_spellings.items() if not kind.endswith("Keyword")
    }
    g4_symbols = {
        match.group(1)
        for match in re.finditer(
            r"^[A-Z][A-Z0-9_]*\s*:\s*'([^a-zA-Z'\\][^']*)'\s*;",
            lexer_g4,
            re.M,
        )
    }

    check_equal_set("hard keywords", spec_keywords, kind_keywords, KINDS_H)
    check_equal_set("hard keywords", spec_keywords, utils_keywords, UTILS_CC)
    check_equal_set("hard keywords", spec_keywords, g4_keywords, ZOM_LEXER_G4)
    check_equal_set("hard keywords", spec_keywords, static_keywords, TOKEN_CC)
    check_equal_set("symbol tokens", spec_symbols, g4_symbols, ZOM_LEXER_G4)
    check_equal_set("symbol tokens", spec_symbols, static_symbols, TOKEN_CC)

    round_trip_kinds = inventory_test_token_kinds(inventory_test)
    check_equal_set(
        "static-token round-trip test kinds",
        set(static_spellings),
        round_trip_kinds,
        LEXER_INVENTORY_TEST,
    )

    form_token_kinds = set(
        re.findall(r"formToken\(ast::SyntaxKind::([A-Za-z][A-Za-z0-9_]*)", lexer_cc)
    )
    get_keyword_kinds = set(
        re.findall(r"return ast::SyntaxKind::([A-Za-z][A-Za-z0-9_]*);", utils_cc)
    )
    produced_kinds = form_token_kinds | get_keyword_kinds
    static_symbol_kinds = {
        kind for kind in static_spellings if not kind.endswith("Keyword") and kind != "EndOfFile"
    }
    non_produced = sorted(static_symbol_kinds - produced_kinds)
    if non_produced:
        fail(
            f"{rel(TOKEN_CC)} defines static text for token kinds the lexer cannot emit: "
            f"{', '.join(non_produced)}"
        )

    g4_rules = set(re.findall(r"^([A-Z][A-Z0-9_]*)\s*:", lexer_g4, re.M))
    missing_dynamic_rules = sorted(EXPECTED_DYNAMIC_G4_RULES - g4_rules)
    if missing_dynamic_rules:
        fail(
            f"{rel(ZOM_LEXER_G4)} is missing dynamic token rules: "
            f"{', '.join(missing_dynamic_rules)}"
        )

    dynamic_sources = "\n".join([lexer_cc, utils_cc])
    for kind, markers in EXPECTED_DYNAMIC_LEXER_KIND_MARKERS.items():
        if not any(marker in dynamic_sources for marker in markers):
            fail(f"{rel(LEXER_DIR / 'lexer.cc')} cannot emit dynamic token kind: {kind}")


def check_lexical_conformance_metadata() -> None:
    lexical_corpus = CONFORMANCE_CORPUS / "02-lexical"
    lexical_grammar = CONFORMANCE_GRAMMAR / "02-lexical"
    lexical_ast = CONFORMANCE_AST / "02-lexical"

    corpus = path_stems(lexical_corpus, ".zom")
    grammar = path_stems(lexical_grammar, ".yml")
    ast = path_stems(lexical_ast, ".check")

    missing_ast = sorted(corpus - ast)
    if missing_ast:
        fail(
            f"{rel(lexical_ast)} is missing AST expectations for lexical corpus files: "
            f"{', '.join(str(path) for path in missing_ast)}"
        )

    missing_grammar = sorted((corpus - LEXICAL_GRAMMAR_EXCEPTIONS) - grammar)
    if missing_grammar:
        fail(
            f"{rel(lexical_grammar)} is missing grammar expectations for lexical corpus files: "
            f"{', '.join(str(path) for path in missing_grammar)}"
        )

    orphan_grammar = sorted(grammar - corpus)
    if orphan_grammar:
        fail(
            f"{rel(lexical_grammar)} contains orphan grammar expectations: "
            f"{', '.join(str(path) for path in orphan_grammar)}"
        )


def check_unicode_data() -> None:
    header = UNICODE_H.read_text(encoding="utf-8")
    implementation = UNICODE_CC.read_text(encoding="utf-8")
    generator = GENERATOR.read_text(encoding="utf-8")

    require_contains(UNICODE_H, header, f'kUnicodeIdentifierDataVersion[] = "{EXPECTED_UCD_VERSION}"')
    require_contains(UNICODE_H, header, EXPECTED_UCD_SOURCE)
    require_contains(UNICODE_CC, implementation, "Generated by scripts/codegen/gen_unicode_data.py")
    require_contains(UNICODE_CC, implementation, f"UCD version: {EXPECTED_UCD_VERSION}")
    require_contains(UNICODE_CC, implementation, EXPECTED_UCD_SOURCE)
    require_contains(GENERATOR, generator, f'DEFAULT_UCD_VERSION = "{EXPECTED_UCD_VERSION}"')
    require_contains(GENERATOR, generator, "DerivedCoreProperties.txt")

    start_count = range_count(implementation, "ID_START_RANGES_DATA")
    part_count = range_count(implementation, "ID_PART_RANGES_DATA")
    if start_count != EXPECTED_START_RANGES:
        fail(f"{rel(UNICODE_CC)} has {start_count} ID start ranges, expected {EXPECTED_START_RANGES}")
    if part_count != EXPECTED_PART_RANGES:
        fail(f"{rel(UNICODE_CC)} has {part_count} ID part ranges, expected {EXPECTED_PART_RANGES}")


def check_public_lexer_contract() -> None:
    public_header = (LEXER_DIR / "lexer.h").read_text(encoding="utf-8")
    for banned in PUBLIC_LEXER_API_BANNED_TERMS:
        if banned in public_header:
            fail(f"{rel(LEXER_DIR / 'lexer.h')} exposes banned lexer API: {banned}")

    for path in parser_source_paths():
        text = path.read_text(encoding="utf-8")
        for banned in PUBLIC_LEXER_API_BANNED_TERMS:
            if banned in text:
                fail(f"{rel(path)} depends on banned lexer API: {banned}")
        for term in PARSER_RAW_SOURCE_ACCESS_TERMS:
            if term in text:
                fail(f"{rel(path)} directly accesses lexer/source buffer state: {term}")


def check_template_mode_contract() -> None:
    implementation = (LEXER_DIR / "lexer.cc").read_text(encoding="utf-8")
    literal_tests = (LEXER_TEST_DIR / "lexer-literal-test.cc").read_text(encoding="utf-8")

    for marker in TEMPLATE_MODE_IMPLEMENTATION_MARKERS:
        require_contains(LEXER_DIR / "lexer.cc", implementation, marker)
    for marker in TEMPLATE_MODE_TEST_MARKERS:
        require_contains(LEXER_TEST_DIR / "lexer-literal-test.cc", literal_tests, marker)


def check_design_stream_contract() -> None:
    for path, markers in DESIGN_DOC_MARKERS.items():
        if not path.exists():
            fail(f"{rel(path)} does not exist")
            continue

        text = path.read_text(encoding="utf-8")
        for marker in markers:
            require_contains(path, text, marker)
        for term in DESIGN_DOC_BANNED_TERMS:
            if term in text:
                fail(f"{rel(path)} contains obsolete lexer stream contract term: {term}")


def main() -> int:
    check_token_inventory_alignment()
    check_lexical_conformance_metadata()
    check_unicode_data()
    check_public_lexer_contract()
    check_template_mode_contract()
    check_design_stream_contract()

    if errors:
        print("Lexer architecture check failed.", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    print(
        "Lexer architecture check passed "
        f"(UCD {EXPECTED_UCD_VERSION}, {EXPECTED_START_RANGES} ID start ranges, "
        f"{EXPECTED_PART_RANGES} ID part ranges)."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
