#!/usr/bin/env python3

import re
import sys
from pathlib import Path

import yaml


ROOT = Path(__file__).resolve().parents[1]
GRAMMAR = ROOT / "docs" / "spec" / "chapters" / "17-grammar-reference.md"
PARSER_DIR = ROOT / "products" / "zomlang" / "compiler" / "parser"
PARSER_IMPL = PARSER_DIR / "parser-impl.h"
TOKEN_CURSOR = PARSER_DIR / "token-cursor.h"
PARSER_SOURCES = sorted(PARSER_DIR.glob("*.cc"))
DOMAIN_PARSER_SOURCES = [
    PARSER_DIR / "declaration-parser.cc",
    PARSER_DIR / "expression-parser.cc",
    PARSER_DIR / "parser-recovery.cc",
    PARSER_DIR / "pattern-parser.cc",
    PARSER_DIR / "statement-parser.cc",
    PARSER_DIR / "type-parser.cc",
]
CURSOR_BOUNDARY_SOURCES = set(PARSER_SOURCES)
COVERAGE = ROOT / "products" / "zomlang" / "compiler" / "parser" / "parser-coverage.yml"
SCHEMA = ROOT / "products" / "zomlang" / "compiler" / "ast" / "schema.yml"
NODE_FACTORY = ROOT / "products" / "zomlang" / "compiler" / "ast" / "generated" / "node-factory.h"
DIAGNOSTIC_ENGINE = ROOT / "products" / "zomlang" / "compiler" / "diagnostics" / "diagnostic-engine.cc"
DESIGN_DIR = ROOT / "docs" / "design"
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
    "Loose Parsing Mode",
    "ParseMode",
    "fail-open",
    "best-effort AST",
    "Own<TokenStream>",
    "Token[] with SourceLoc",
    "Immutable post-lex",
    "pre-lexing the whole file",
    "lexAll",
]

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


def parser_source_text() -> str:
    return "\n".join(path.read_text(encoding="utf-8") for path in PARSER_SOURCES)


def validate_parser_architecture() -> None:
    impl_text = PARSER_IMPL.read_text(encoding="utf-8")
    cursor_text = TOKEN_CURSOR.read_text(encoding="utf-8")
    if "with all inline methods" in impl_text:
        fail(f"{rel(PARSER_IMPL)} still describes Parser::Impl methods as inline")
    if "class AstFactory final" not in impl_text:
        fail(f"{rel(PARSER_IMPL)} does not define the parser AstFactory boundary")
    if re.search(r"\n\s+ast::NodeId\s+makeNode\s*\(", impl_text):
        fail(f"{rel(PARSER_IMPL)} exposes a generic AstFactory::makeNode escape hatch")
    if re.search(r"\n\s+void\s+write(Node|String|Ident|BigInt|Float|NodeList|IdentList)\s*\(", impl_text):
        fail(f"{rel(PARSER_IMPL)} exposes raw AST payload writer helpers")
    if "findTopLevel" in impl_text:
        fail(f"{rel(PARSER_IMPL)} exposes index-based top-level range scanning helpers")
    if "tokenCountWithoutEof" in cursor_text or re.search(r"\btokenCount\s*\(", cursor_text):
        fail(f"{rel(TOKEN_CURSOR)} exposes parser-facing force-EOF token counting")
    if re.search(r"\bsize\s*\(\s*\)\s+const", cursor_text):
        fail(f"{rel(TOKEN_CURSOR)} exposes whole-stream cursor sizing")
    for required in [
        "enum class RecoveryContext",
        "struct RecoveryFrame",
        "class RecoveryFrameScope",
        "mutable zc::Vector<RecoveryFrame> recoveryFrames",
    ]:
        if required not in impl_text:
            fail(f"{rel(PARSER_IMPL)} is missing parser recovery frame contract: {required}")
    if impl_text.count("builder.makeNode(") != 1:
        fail(f"{rel(PARSER_IMPL)} must route AST node creation only through makeTypedNode()")
    if not NODE_FACTORY.exists():
        fail(f"{rel(NODE_FACTORY)} does not exist")
    else:
        factory_text = NODE_FACTORY.read_text(encoding="utf-8")
        if "class TypedNodeFactory" not in factory_text:
            fail(f"{rel(NODE_FACTORY)} does not define the schema-generated typed factory")
        for kind in sorted(ast_kinds()):
            if f"make{kind}(" not in factory_text:
                fail(f"{rel(NODE_FACTORY)} does not define make{kind}()")

    for path in DOMAIN_PARSER_SOURCES:
        text = path.read_text(encoding="utf-8")
        if "Intentionally empty" in text:
            fail(f"{rel(path)} is still an empty parser domain shell")
        if "Parser::Impl::" not in text:
            fail(f"{rel(path)} does not define parser implementation methods")

    for path in PARSER_SOURCES:
        text = path.read_text(encoding="utf-8")
        if "ast::TreeBuilder" in text:
            fail(f"{rel(path)} directly depends on ast::TreeBuilder instead of AstFactory")
        if "ast::NodePayload" in text or "payload.words" in text:
            fail(f"{rel(path)} writes raw AST payload layout instead of typed factory methods")
        if re.search(r"(?<!\.)\bwrite(Node|String|Ident|BigInt|Float|NodeList|IdentList)\s*\(", text):
            fail(f"{rel(path)} calls a raw AST payload writer outside AstFactory")
        if "builder.makeNode(" in text:
            fail(f"{rel(path)} constructs AST nodes through the generic TreeBuilder API")
        if "lexAll" in text:
            fail(f"{rel(path)} reintroduces eager parser-side tokenization")
        if "tokenCountWithoutEof" in text or re.search(r"\btokenCount\s*\(", text):
            fail(f"{rel(path)} reintroduces parser-side force-EOF token counting")
        if "TokenCursor::size" in text:
            fail(f"{rel(path)} reintroduces whole-stream cursor sizing")
        if (
            "LexerState" in text
            or "restoreState" in text
            or "getCurrentState" in text
            or "reScanGreaterToken" in text
            or "reScanTemplateToken" in text
        ):
            fail(f"{rel(path)} performs parser-side lexer snapshot or rescan lookahead")
        if path in CURSOR_BOUNDARY_SOURCES and "findTopLevel" in text:
            fail(f"{rel(path)} reintroduces top-level range scanning instead of cursor boundaries")

    recovery_text = (PARSER_DIR / "parser-recovery.cc").read_text(encoding="utf-8")
    for required in ["syncSet", "consumed", "suppressedUntil", "pushRecoveryFrame", "popRecoveryFrame"]:
        if required not in recovery_text:
            fail(f"{rel(PARSER_DIR / 'parser-recovery.cc')} is missing recovery frame state: {required}")
    for path in [
        PARSER_DIR / "declaration-parser.cc",
        PARSER_DIR / "expression-parser.cc",
        PARSER_DIR / "pattern-parser.cc",
        PARSER_DIR / "statement-parser.cc",
        PARSER_DIR / "type-parser.cc",
    ]:
        if "RecoveryFrameScope" not in path.read_text(encoding="utf-8"):
            fail(f"{rel(path)} does not install an explicit parser recovery frame")

    diagnostic_text = DIAGNOSTIC_ENGINE.read_text(encoding="utf-8")
    for required in ["errorBudget = 100", "hasEmitted", "EmittedDiagnosticKey"]:
        if required not in diagnostic_text:
            fail(f"{rel(DIAGNOSTIC_ENGINE)} is missing bounded diagnostic recovery gate: {required}")


def validate_parser_design_docs() -> None:
    loose_parsing_doc = DESIGN_DIR / "loose-parsing-mode.md"
    if loose_parsing_doc.exists():
        fail(f"{rel(loose_parsing_doc)} reintroduces a public loose parsing design")

    for path, markers in DESIGN_DOC_MARKERS.items():
        if not path.exists():
            fail(f"{rel(path)} does not exist")
            continue

        text = path.read_text(encoding="utf-8")
        for marker in markers:
            if marker not in text:
                fail(f"{rel(path)} is missing parser stream contract marker: {marker}")
        for term in DESIGN_DOC_BANNED_TERMS:
            if term in text:
                fail(f"{rel(path)} contains obsolete parser design term: {term}")


def parser_functions() -> set[str]:
    text = parser_source_text()
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
    text = parser_source_text()
    constructed = set(re.findall(r"SyntaxKind::([A-Za-z][A-Za-z0-9_]*)", text))
    constructed.update(re.findall(r"\bmake([A-Z][A-Za-z0-9_]*)\s*\(", text))
    return constructed


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
            fail(f"{name}: parser function '{parser}' is not present in parser sources")

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
            fail(f"{name}: mapped AST kind is not constructed by parser sources: {kind}")


errors: list[str] = []


def fail(message: str) -> None:
    errors.append(message)


def main() -> int:
    validate_parser_architecture()
    validate_parser_design_docs()
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
