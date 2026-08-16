#!/usr/bin/env python3

import argparse
import os
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCHEMA = Path("products/zomlang/compiler/ast/schema.yml")
PAYLOAD = Path("products/zomlang/compiler/ast/generated/node-payload.h")
FACTORY = Path("products/zomlang/compiler/ast/generated/node-factory.h")
DECLARATION_PARSER = Path("products/zomlang/compiler/parser/declaration-parser.cc")
STATEMENT_PARSER = Path("products/zomlang/compiler/parser/statement-parser.cc")
ANTLR_PARSER = Path("docs/spec/ZomParser.g4")
DECLARATIONS_SPEC = Path("docs/spec/chapters/06-declarations.md")
INTERFACES_SPEC = Path("docs/spec/chapters/09-interfaces.md")
ATTRIBUTES_SPEC = Path("docs/spec/chapters/16-attributes-and-annotations.md")
GRAMMAR_REFERENCE = Path("docs/spec/chapters/17-grammar-reference.md")
SIGNATURE_FACTS = Path("products/zomlang/compiler/checker/signature-facts.cc")
PARSE_DIAGNOSTICS = Path("products/zomlang/compiler/diagnostics/defs/diagnostics-parse.def")
PARSER_TEST = Path("products/zomlang/tests/unittests/compiler/parser/parser-test.cc")
DRIVER_TEST = Path(
    "products/zomlang/tests/unittests/compiler/driver/compiler-session-package-test.cc"
)
CONFORMANCE_ROOT = Path("products/zomlang/tests/conformance/corpus")
AST_ROOT = Path("products/zomlang/tests/conformance/expectations/ast")
GRAMMAR_ROOT = Path("products/zomlang/tests/conformance/expectations/grammar")
DIAGNOSTICS_ROOT = Path("products/zomlang/tests/conformance/expectations/diagnostics")
MULTI_INTERFACE_NEGATIVE = CONFORMANCE_ROOT / Path(
    "09-interfaces/impl_multiple_interfaces_neg_23.zom"
)


def read(path: Path) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def node_schema(schema: str, name: str) -> str:
    match = re.search(
        rf"(?ms)^  - id: [^\n]+\n    name: {re.escape(name)}\n.*?(?=^  - id: |\Z)",
        schema,
    )
    return "" if match is None else match.group(0)


def production_sources() -> dict[Path, str]:
    files = {
        SCHEMA: read(SCHEMA),
        PAYLOAD: read(PAYLOAD),
        FACTORY: read(FACTORY),
        DECLARATION_PARSER: read(DECLARATION_PARSER),
        STATEMENT_PARSER: read(STATEMENT_PARSER),
        ANTLR_PARSER: read(ANTLR_PARSER),
        DECLARATIONS_SPEC: read(DECLARATIONS_SPEC),
        INTERFACES_SPEC: read(INTERFACES_SPEC),
        ATTRIBUTES_SPEC: read(ATTRIBUTES_SPEC),
        GRAMMAR_REFERENCE: read(GRAMMAR_REFERENCE),
        SIGNATURE_FACTS: read(SIGNATURE_FACTS),
        PARSE_DIAGNOSTICS: read(PARSE_DIAGNOSTICS),
        PARSER_TEST: read(PARSER_TEST),
        DRIVER_TEST: read(DRIVER_TEST),
    }
    compiler = ROOT / "products/zomlang/compiler"
    for directory, names, basenames in os.walk(compiler):
        names[:] = [name for name in names if name != "vendor"]
        for basename in basenames:
            if not basename.endswith((".h", ".cc")):
                continue
            path = Path(directory) / basename
            files[path.relative_to(ROOT)] = path.read_text(encoding="utf-8")
    for path in (ROOT / CONFORMANCE_ROOT).rglob("*.zom"):
        files[path.relative_to(ROOT)] = path.read_text(encoding="utf-8")
    for path in (ROOT / AST_ROOT).rglob("*.check"):
        files[path.relative_to(ROOT)] = path.read_text(encoding="utf-8")
    for path in (ROOT / GRAMMAR_ROOT).rglob("*.yml"):
        files[path.relative_to(ROOT)] = path.read_text(encoding="utf-8")
    for path in (ROOT / DIAGNOSTICS_ROOT).rglob("*.check"):
        files[path.relative_to(ROOT)] = path.read_text(encoding="utf-8")
    return files


def require(text: str, marker: str, path: Path, errors: list[str]) -> None:
    if marker not in text:
        errors.append(f"{path}: missing singular impl marker: {marker}")


def forbid(text: str, marker: str, path: Path, errors: list[str]) -> None:
    if marker in text:
        errors.append(f"{path}: removed impl source marker remains: {marker}")


def check_schema(files: dict[Path, str], errors: list[str]) -> None:
    schema = files[SCHEMA]
    ordinary = node_schema(schema, "StandaloneImplDecl")
    marker = node_schema(schema, "MarkerImpl")
    heritage = node_schema(schema, "ImplIfaceList")

    require(ordinary, "{name: interface, type: NodeId, cast: TypeExpr}", SCHEMA, errors)
    forbid(ordinary, "name: ifaces_id", SCHEMA, errors)
    require(marker, "{name: marker_path, type: NodeId, cast: AttributePath}", SCHEMA, errors)
    require(marker, "{name: for_ty, type: NodeId, cast: TypeExpr}", SCHEMA, errors)
    forbid(marker, "name: where_", SCHEMA, errors)
    forbid(marker, "name: type_params_id", SCHEMA, errors)
    require(heritage, "{name: ifaces, type: NodeList, cast: TypeExpr}", SCHEMA, errors)
    forbid(heritage, "name: n_ifaces", SCHEMA, errors)

    require(files[PAYLOAD], "kStandaloneImplDeclInterfaceWord", PAYLOAD, errors)
    require(
        files[FACTORY],
        "makeMarkerImpl(source::SourceRange range, bool is_unsafe, bool is_negated, "
        "NodeId marker_path, NodeId for_ty)",
        FACTORY,
        errors,
    )


def check_parser_and_diagnostics(files: dict[Path, str], errors: list[str]) -> None:
    parser = files[DECLARATION_PARSER]
    for marker in (
        "DiagID::ImplRequiresSingleInterface",
        "DiagID::MarkerImplCannotBeGeneric",
        "DiagID::MarkerImplCannotHaveWhereClause",
        "DiagID::NegativeMarkerImplCannotBeUnsafe",
        "DiagID::MarkerImplCannotHaveBody",
        "builder.makeStandaloneImplDecl(rangeFor(start, end), isUnsafe, interface",
        "builder.makeMarkerImpl(rangeFor(start, end), isUnsafe, isNegated",
    ):
        require(parser, marker, DECLARATION_PARSER, errors)
    forbid(parser, "makeImplIfaceList(builder, ifaceStart", DECLARATION_PARSER, errors)
    marker_parser_start = parser.find("Parser::Impl::parseMarkerImplDeclaration")
    marker_parser_end = parser.find("\n}\n\n}  // namespace parser", marker_parser_start)
    if marker_parser_start < 0 or marker_parser_end < 0:
        errors.append(f"{DECLARATION_PARSER}: marker impl parser function is missing")
    else:
        marker_parser = parser[marker_parser_start:marker_parser_end]
        marker_validation_start = marker_parser.find("const size_t markerEnd")
        marker_validation_end = marker_parser.find("TokenCursor forCursor")
        if marker_validation_start < 0 or marker_validation_end < 0:
            errors.append(f"{DECLARATION_PARSER}: marker path validation boundary is missing")
        else:
            marker_validation = marker_parser[marker_validation_start:marker_validation_end]
            if (
                marker_validation.count("if (") != 1
                or "if (markerEnd <= markerStart)" not in marker_validation
            ):
                errors.append(
                    f"{DECLARATION_PARSER}: marker path validation must not gate short paths"
                )
        if "segmentCount" in marker_parser:
            errors.append(
                f"{DECLARATION_PARSER}: marker parser restored a path-length admission gate"
            )
    require(
        files[STATEMENT_PARSER],
        "builder.makeImplIfaceList(rangeFor(start, end), builder.makeList(ifaces.asPtr()))",
        STATEMENT_PARSER,
        errors,
    )

    diagnostics = files[PARSE_DIAGNOSTICS]
    expected = (
        (2100, "ImplRequiresSingleInterface", "An impl declaration implements exactly one interface"),
        (2101, "MarkerImplCannotBeGeneric", "A marker implementation cannot declare type parameters"),
        (2102, "MarkerImplCannotHaveWhereClause", "A marker implementation cannot have a where clause"),
        (2103, "NegativeMarkerImplCannotBeUnsafe", "A marker implementation cannot be unsafe"),
        (2104, "MarkerImplCannotHaveBody", "A marker implementation cannot have a body"),
    )
    for code, name, message in expected:
        require(diagnostics, f"DIAG({code}, {name}, kError,", PARSE_DIAGNOSTICS, errors)
        require(diagnostics, f'"{message}"', PARSE_DIAGNOSTICS, errors)

    grammar = files[ANTLR_PARSER]
    marker_rule = re.search(r"(?ms)^markerImplRest\n.*?(?=^[a-zA-Z][a-zA-Z0-9_]*\n)", grammar)
    if marker_rule is None or "NOT?" not in marker_rule.group(0):
        errors.append(f"{ANTLR_PARSER}: positive marker candidate admission is missing")
    if "markerImplRest" not in grammar or "# markerImplPlain" not in grammar:
        errors.append(f"{ANTLR_PARSER}: plain marker candidate route is missing")

    signature = files[SIGNATURE_FACTS]
    behavior = signature.find("SignatureSourceDiagnostic::BehaviorInterfaceRequiresImplBody")
    unsafe = signature.find("SignatureSourceDiagnostic::PositiveMarkerImplRequiresUnsafe")
    if behavior < 0 or unsafe < 0 or behavior >= unsafe:
        errors.append(
            f"{SIGNATURE_FACTS}: behavior-body rejection must precede positive marker safety"
        )


def check_removed_fields(files: dict[Path, str], errors: list[str]) -> None:
    forbidden = (
        "kStandaloneImplDeclIfacesIdWord",
        "kMarkerImplWhereWord",
        "kMarkerImplTypeParamsIdWord",
        "kImplIfaceListNIfacesWord",
    )
    for path, text in sorted(files.items()):
        if path == Path(__file__).relative_to(ROOT):
            continue
        for marker in forbidden:
            if marker in text:
                errors.append(f"{path}: removed generated impl field remains: {marker}")


def check_spec_contract(files: dict[Path, str], errors: list[str]) -> None:
    declarations = files[DECLARATIONS_SPEC]
    require(
        declarations,
        "StandaloneImplDecl ::= UnsafePrefix? 'impl' TypeParameters? InterfaceBound 'for' TypeExpr",
        DECLARATIONS_SPEC,
        errors,
    )
    require(
        declarations,
        "PositiveMarkerImplDecl ::= UnsafePrefix? 'impl' MarkerImplPath 'for' ClosedTypeExpr ';'",
        DECLARATIONS_SPEC,
        errors,
    )
    require(
        declarations,
        "NegativeMarkerImplDecl ::= 'impl' '!' MarkerImplPath 'for' ClosedTypeExpr ';'",
        DECLARATIONS_SPEC,
        errors,
    )
    require(
        declarations,
        "the parser retains a\npositive candidate without that prefix",
        DECLARATIONS_SPEC,
        errors,
    )
    forbid(
        declarations,
        "StandaloneImplDecl ::= UnsafePrefix? 'impl' TypeParameters? InterfaceBoundList",
        DECLARATIONS_SPEC,
        errors,
    )

    interfaces = files[INTERFACES_SPEC]
    require(
        interfaces,
        "followed by exactly one\ninterface instantiation",
        INTERFACES_SPEC,
        errors,
    )
    require(
        interfaces,
        "Every ordinary impl names exactly one behavior interface",
        INTERFACES_SPEC,
        errors,
    )
    forbid(
        interfaces,
        "StandaloneImplDecl ::= UnsafePrefix? 'impl' TypeParameters? InterfaceBoundList",
        INTERFACES_SPEC,
        errors,
    )

    attributes = files[ATTRIBUTES_SPEC]
    require(
        attributes,
        "PositiveMarkerImpl ::= UnsafePrefix? 'impl' MarkerImplPath",
        ATTRIBUTES_SPEC,
        errors,
    )
    require(
        attributes,
        "Marker impls have no type parameters, `where` clause, associated bindings,\nmembers, or body.",
        ATTRIBUTES_SPEC,
        errors,
    )
    require(
        attributes,
        "A semicolon selects a marker candidate; an opening body\nselects an ordinary interface impl.",
        ATTRIBUTES_SPEC,
        errors,
    )
    forbid(attributes, "NegativeMarkerImpl ::= 'unsafe'?", ATTRIBUTES_SPEC, errors)

    grammar = files[GRAMMAR_REFERENCE]
    require(
        grammar,
        "::= 'unsafe'? 'impl' MarkerImplPath 'for' TypeExpression ';'",
        GRAMMAR_REFERENCE,
        errors,
    )
    require(
        grammar,
        "::= 'unsafe'? 'impl' TypeParameters? InterfaceBound 'for' TypeExpression",
        GRAMMAR_REFERENCE,
        errors,
    )
    require(
        grammar,
        "The parser retains a positive candidate without\n   'unsafe'.",
        GRAMMAR_REFERENCE,
        errors,
    )
    forbid(
        grammar,
        "::= 'unsafe'? 'impl' TypeParameters? InterfaceBoundList 'for' TypeExpression",
        GRAMMAR_REFERENCE,
        errors,
    )


def check_conformance(files: dict[Path, str], errors: list[str]) -> None:
    test = files[PARSER_TEST]
    for marker in (
        "ParserTest.ImplDeclarationsUseSingularAstShapes",
        "ParserTest.ImplShapeDiagnosticsAreClosedAndSuppressRecovery",
        "ParserTest.InterfaceHeritageExceedsUint8WithoutTruncation",
        "ParserTest.PositiveMarkerCandidatesReachSignatureCheckingWithoutUnsafe",
    ):
        require(test, marker, PARSER_TEST, errors)

    require(
        files[DRIVER_TEST],
        "CompilerSession routes short and qualified safe marker candidates to ZOM4091",
        DRIVER_TEST,
        errors,
    )
    require(
        files[DRIVER_TEST],
        "CompilerSession gives behavior body diagnostics precedence over marker safety",
        DRIVER_TEST,
        errors,
    )

    for path, text in sorted(files.items()):
        if not path.is_relative_to(CONFORMANCE_ROOT) or path.suffix != ".zom":
            continue
        for line in text.splitlines():
            stripped = line.strip()
            if not re.match(r"^(?:unsafe\s+)?impl(?:<[^>]*>)?\s+", stripped):
                continue
            header = stripped.split("{", 1)[0]
            if " for " in header and "+" in header and path != MULTI_INTERFACE_NEGATIVE:
                errors.append(f"{path}: ordinary impl header contains '+' outside its negative fixture")

    required_sources = (
        "09-interfaces/impl_multiple_interfaces_neg_23.zom",
        "09-interfaces/marker_impl_generic_neg_24.zom",
        "09-interfaces/marker_impl_where_neg_25.zom",
        "09-interfaces/negative_marker_impl_unsafe_neg_26.zom",
        "09-interfaces/negative_marker_impl_body_neg_27.zom",
        "09-interfaces/marker_impl_precedence_neg_28.zom",
        "09-interfaces/marker_impl_short_requires_unsafe_neg_29.zom",
        "09-interfaces/marker_impl_qualified_requires_unsafe_neg_30.zom",
        "09-interfaces/marker_impl_behavior_precedence_neg_31.zom",
    )
    for relative in required_sources:
        path = CONFORMANCE_ROOT / relative
        if path not in files:
            errors.append(f"{path}: required singular impl conformance fixture is missing")

    marker_cases = (
        ("marker_impl_short_requires_unsafe_neg_29", "[ZOM4091]"),
        ("marker_impl_qualified_requires_unsafe_neg_30", "[ZOM4091]"),
        ("marker_impl_behavior_precedence_neg_31", "[ZOM4089]"),
    )
    for stem, diagnostic in marker_cases:
        source_path = CONFORMANCE_ROOT / "09-interfaces" / f"{stem}.zom"
        ast_path = AST_ROOT / "09-interfaces" / f"{stem}.check"
        grammar_path = GRAMMAR_ROOT / "09-interfaces" / f"{stem}.yml"
        diagnostic_path = DIAGNOSTICS_ROOT / "09-interfaces" / f"{stem}.check"
        for path, description in (
            (source_path, "source fixture"),
            (ast_path, "AST expectation"),
            (grammar_path, "grammar expectation"),
            (diagnostic_path, "diagnostic expectation"),
        ):
            if path not in files:
                errors.append(f"{path}: required marker admission {description} is missing")
        if source_path in files:
            forbid(files[source_path], "unsafe impl", source_path, errors)
        if ast_path in files:
            require(files[ast_path], "is_unsafe=false is_negated=false", ast_path, errors)
        if grammar_path in files:
            require(files[grammar_path], 'expected: "ACCEPT"', grammar_path, errors)
        if diagnostic_path in files:
            require(files[diagnostic_path], diagnostic, diagnostic_path, errors)
            require(files[diagnostic_path], "compile --emit=dispatch", diagnostic_path, errors)
    behavior_diagnostic = (
        DIAGNOSTICS_ROOT
        / "09-interfaces/marker_impl_behavior_precedence_neg_31.check"
    )
    if behavior_diagnostic in files:
        require(files[behavior_diagnostic], "CHECK-NOT: ZOM4091", behavior_diagnostic, errors)


def analyze(files: dict[Path, str]) -> list[str]:
    errors: list[str] = []
    check_schema(files, errors)
    check_parser_and_diagnostics(files, errors)
    check_removed_fields(files, errors)
    check_spec_contract(files, errors)
    check_conformance(files, errors)
    return sorted(set(errors))


def run_check() -> int:
    errors = analyze(production_sources())
    if errors:
        print("Impl source architecture check failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    print("Impl source architecture check passed (singular ordinary and bodyless marker impls).")
    return 0


def remove_once(files: dict[Path, str], path: Path, marker: str) -> None:
    files[path] = files[path].replace(marker, "", 1)


def expect_rejection(
    baseline: dict[Path, str], name: str, mutate, expected: str
) -> list[str]:
    fixture = dict(baseline)
    mutate(fixture)
    errors = analyze(fixture)
    if any(expected in error for error in errors):
        return []
    return [f"negative fixture {name!r} was not rejected for {expected!r}"]


def run_self_test() -> int:
    baseline = production_sources()
    failures = analyze(baseline)
    if failures:
        print("Impl source architecture self-test baseline failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    failures += expect_rejection(
        baseline,
        "ordinary interface field removed",
        lambda files: remove_once(files, SCHEMA, "{name: interface, type: NodeId, cast: TypeExpr}"),
        "missing singular impl marker",
    )
    failures += expect_rejection(
        baseline,
        "marker generic field restored",
        lambda files: files.__setitem__(
            SCHEMA,
            files[SCHEMA].replace(
                "      - {name: marker_path, type: NodeId, cast: AttributePath}\n"
                "      - {name: for_ty, type: NodeId, cast: TypeExpr}\n",
                "      - {name: marker_path, type: NodeId, cast: AttributePath}\n"
                "      - {name: for_ty, type: NodeId, cast: TypeExpr}\n"
                "      - {name: type_params_id, type: NodeId}\n",
                1,
            ),
        ),
        "name: type_params_id",
    )
    failures += expect_rejection(
        baseline,
        "heritage count restored",
        lambda files: files.__setitem__(
            SCHEMA,
            files[SCHEMA].replace(
                "    name: ImplIfaceList\n"
                "    doc:  \"A + B + C interface heritage list\"\n"
                "    fields:\n"
                "      - {name: ifaces, type: NodeList, cast: TypeExpr}\n",
                "    name: ImplIfaceList\n"
                "    doc:  \"A + B + C interface heritage list\"\n"
                "    fields:\n"
                "      - {name: n_ifaces, type: uint8}\n"
                "      - {name: ifaces, type: NodeList, cast: TypeExpr}\n",
                1,
            ),
        ),
        "name: n_ifaces",
    )
    failures += expect_rejection(
        baseline,
        "parser diagnostic removed",
        lambda files: remove_once(files, DECLARATION_PARSER, "DiagID::ImplRequiresSingleInterface"),
        "missing singular impl marker",
    )
    failures += expect_rejection(
        baseline,
        "diagnostic registry removed",
        lambda files: remove_once(files, PARSE_DIAGNOSTICS, "DIAG(2104, MarkerImplCannotHaveBody, kError,"),
        "missing singular impl marker",
    )
    failures += expect_rejection(
        baseline,
        "generated old field restored",
        lambda files: files.__setitem__(PAYLOAD, files[PAYLOAD] + "\nkImplIfaceListNIfacesWord\n"),
        "removed generated impl field remains",
    )
    failures += expect_rejection(
        baseline,
        "precedence fixture removed",
        lambda files: files.pop(CONFORMANCE_ROOT / "09-interfaces/marker_impl_precedence_neg_28.zom"),
        "required singular impl conformance fixture is missing",
    )
    failures += expect_rejection(
        baseline,
        "positive marker admission removed",
        lambda files: remove_once(files, ANTLR_PARSER, "NOT?"),
        "positive marker candidate admission is missing",
    )
    failures += expect_rejection(
        baseline,
        "short marker parser gate restored",
        lambda files: files.__setitem__(
            DECLARATION_PARSER,
            files[DECLARATION_PARSER].replace(
                "  TokenCursor forCursor = tokenCursorAt(markerEnd);",
                "  if (markerEnd - markerStart < 2) { return ast::NodeId(); }\n\n"
                "  TokenCursor forCursor = tokenCursorAt(markerEnd);",
                1,
            ),
        ),
        "marker path validation must not gate short paths",
    )
    failures += expect_rejection(
        baseline,
        "checker routing test removed",
        lambda files: remove_once(
            files,
            DRIVER_TEST,
            "CompilerSession routes short and qualified safe marker candidates to ZOM4091",
        ),
        "missing singular impl marker",
    )
    failures += expect_rejection(
        baseline,
        "behavior precedence control removed",
        lambda files: remove_once(
            files,
            DRIVER_TEST,
            "CompilerSession gives behavior body diagnostics precedence over marker safety",
        ),
        "missing singular impl marker",
    )
    failures += expect_rejection(
        baseline,
        "short marker source removed",
        lambda files: files.pop(
            CONFORMANCE_ROOT / "09-interfaces/marker_impl_short_requires_unsafe_neg_29.zom"
        ),
        "required marker admission source fixture is missing",
    )
    failures += expect_rejection(
        baseline,
        "qualified marker AST expectation removed",
        lambda files: files.pop(
            AST_ROOT / "09-interfaces/marker_impl_qualified_requires_unsafe_neg_30.check"
        ),
        "required marker admission AST expectation is missing",
    )
    failures += expect_rejection(
        baseline,
        "short marker grammar expectation removed",
        lambda files: files.pop(
            GRAMMAR_ROOT / "09-interfaces/marker_impl_short_requires_unsafe_neg_29.yml"
        ),
        "required marker admission grammar expectation is missing",
    )
    failures += expect_rejection(
        baseline,
        "qualified marker diagnostic expectation removed",
        lambda files: files.pop(
            DIAGNOSTICS_ROOT
            / "09-interfaces/marker_impl_qualified_requires_unsafe_neg_30.check"
        ),
        "required marker admission diagnostic expectation is missing",
    )
    failures += expect_rejection(
        baseline,
        "behavior diagnostic precedence weakened",
        lambda files: remove_once(
            files,
            DIAGNOSTICS_ROOT
            / "09-interfaces/marker_impl_behavior_precedence_neg_31.check",
            "CHECK-NOT: ZOM4091",
        ),
        "missing singular impl marker",
    )
    failures += expect_rejection(
        baseline,
        "declaration spec singular impl removed",
        lambda files: remove_once(
            files,
            DECLARATIONS_SPEC,
            "StandaloneImplDecl ::= UnsafePrefix? 'impl' TypeParameters? InterfaceBound 'for' TypeExpr",
        ),
        "missing singular impl marker",
    )
    failures += expect_rejection(
        baseline,
        "interface spec singular ownership removed",
        lambda files: remove_once(
            files,
            INTERFACES_SPEC,
            "Every ordinary impl names exactly one behavior interface",
        ),
        "missing singular impl marker",
    )
    failures += expect_rejection(
        baseline,
        "attribute spec marker delimiter removed",
        lambda files: remove_once(
            files,
            ATTRIBUTES_SPEC,
            "A semicolon selects a marker candidate; an opening body\nselects an ordinary interface impl.",
        ),
        "missing singular impl marker",
    )
    failures += expect_rejection(
        baseline,
        "grammar reference ordinary impl list restored",
        lambda files: files.__setitem__(
            GRAMMAR_REFERENCE,
            files[GRAMMAR_REFERENCE].replace(
                "::= 'unsafe'? 'impl' TypeParameters? InterfaceBound 'for' TypeExpression",
                "::= 'unsafe'? 'impl' TypeParameters? InterfaceBoundList 'for' TypeExpression",
                1,
            ),
        ),
        "removed impl source marker remains",
    )

    if failures:
        print("Impl source architecture self-test failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print("Impl source architecture negative fixtures passed (20/20).")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    return run_check() if args.check else run_self_test()


if __name__ == "__main__":
    raise SystemExit(main())
