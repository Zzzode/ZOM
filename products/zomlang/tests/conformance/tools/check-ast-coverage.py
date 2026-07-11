#!/usr/bin/env python3

import re
import sys
from pathlib import Path


CONFORMANCE_ROOT = Path(__file__).resolve().parents[1]
CORPUS_ROOT = CONFORMANCE_ROOT / "corpus"
AST_EXPECTATION_ROOT = CONFORMANCE_ROOT / "expectations" / "ast"
GRAMMAR_EXPECTATION_ROOT = CONFORMANCE_ROOT / "expectations" / "grammar"

EXPECTED_RE = re.compile(r'^expected:\s*"?([A-Z]+)"?', re.MULTILINE)
EXPECTED_DIAGNOSTIC_RE = re.compile(r"^expected_diagnostic:", re.MULTILINE)

ALLOWED_EXTRA_AST_CHECKS = {
    Path("00-dump-format/default-tree.check"),
    Path("00-dump-format/invalid-format.check"),
    Path("00-dump-format/json.check"),
    Path("00-dump-format/raw.check"),
}

ALLOWED_AST_WITHOUT_GRAMMAR_CHECKS = {
    Path("02-lexical/identifiers/reserved-words.check"),
    Path("03-types/function-types.check"),
    Path("03-types/object-types.check"),
    Path("03-types/tuple-types.check"),
    Path("03-types/type-forms-error.check"),
    Path("03-types/type-forms.check"),
    Path("03-types/type-query.check"),
    Path("03-types/union-complex.check"),
    Path("03-types/union-intersection.check"),
    Path("03-types/union-precedence.check"),
    Path("03-types/union-trailing-bar.check"),
    Path("04-expressions/arrays-and-objects.check"),
    Path("04-expressions/assignment-operators.check"),
    Path("04-expressions/binary-operators.check"),
    Path("04-expressions/calls-members-new-optional.check"),
    Path("04-expressions/error-handling-operators.check"),
    Path("04-expressions/literals/bigint-normalization.check"),
    Path("04-expressions/literals/bigint.check"),
    Path("04-expressions/literals/character-literals.check"),
    Path("04-expressions/literals/integer-formats.check"),
    Path("04-expressions/literals/legacy-octal.check"),
    Path("04-expressions/literals/numeric-literals.check"),
    Path("04-expressions/literals/string-escapes.check"),
    Path("04-expressions/literals/string-literals.check"),
    Path("04-expressions/strict-and-error-default.check"),
    Path("04-expressions/template-literals.check"),
    Path("04-expressions/unary-and-cast.check"),
    Path("05-statements/basic-statements.check"),
    Path("05-statements/break-continue.check"),
    Path("05-statements/control-flow/conditionals.check"),
    Path("05-statements/control-flow/loops/break-continue.check"),
    Path("05-statements/control-flow/loops.check"),
    Path("05-statements/control-flow.check"),
    Path("05-statements/jumps.check"),
    Path("05-statements/loops.check"),
    Path("05-statements/match.check"),
    Path("05-statements/test_hang.check"),
    Path("05-statements/test_match.check"),
    Path("06-declarations/aliases/alias-declarations.check"),
    Path("06-declarations/enums/enum-declarations.check"),
    Path("06-declarations/errors/error-declarations.check"),
    Path("06-declarations/functions/function-definitions/default-params.check"),
    Path("06-declarations/functions/function-definitions/function.check"),
    Path("06-declarations/functions/function-definitions/generic.check"),
    Path("06-declarations/functions/function-definitions/no-return.check"),
    Path("06-declarations/functions/function-definitions/raises.check"),
    Path("06-declarations/structs/struct-declarations.check"),
    Path("06-declarations/variables/destructuring.check"),
    Path("06-declarations/variables/variable-declarations.check"),
    Path("08-adt/classes/class-accessors-init-deinit.check"),
    Path("08-adt/classes/class-declaration.check"),
    Path("08-adt/classes/class-inheritance.check"),
    Path("08-adt/classes/class-member-tags.check"),
    Path("08-adt/classes/modifiers.check"),
    Path("09-interfaces/errors/interface-invalid-braces.check"),
    Path("09-interfaces/errors/interface-invalid-member-class.check"),
    Path("09-interfaces/errors/interface-invalid-method-type.check"),
    Path("09-interfaces/errors/interface-method-extra-initializer.check"),
    Path("09-interfaces/errors/interface-method-missing-parens.check"),
    Path("09-interfaces/errors/interface-missing-body-close-brace.check"),
    Path("09-interfaces/errors/interface-missing-body-open-brace.check"),
    Path("09-interfaces/errors/interface-property-missing-name.check"),
    Path("09-interfaces/interface-declaration.check"),
    Path("09-interfaces/interface-inheritance.check"),
    Path("09-interfaces/interface-member-tags.check"),
    Path("09-interfaces/interface-modifiers-optional.check"),
    Path("13-modules/import-export.check"),
}


def rel_files(root: Path, suffix: str) -> set[Path]:
    return {
        path.relative_to(root)
        for path in root.rglob(f"*{suffix}")
        if path.is_file()
    }


def print_examples(header: str, entries: list[Path]) -> None:
    if not entries:
        return

    print(f"{header} ({len(entries)}):", file=sys.stderr)
    for path in entries[:50]:
        print(f"  - {path.as_posix()}", file=sys.stderr)
    if len(entries) > 50:
        print(f"  ... and {len(entries) - 50} more", file=sys.stderr)


def print_text_examples(header: str, entries: list[str]) -> None:
    if not entries:
        return

    print(f"{header} ({len(entries)}):", file=sys.stderr)
    for entry in entries[:50]:
        print(f"  - {entry}", file=sys.stderr)
    if len(entries) > 50:
        print(f"  ... and {len(entries) - 50} more", file=sys.stderr)


def load_grammar_expectations() -> tuple[dict[Path, str], list[str]]:
    expectations: dict[Path, str] = {}
    invalid: list[str] = []

    for path in sorted(GRAMMAR_EXPECTATION_ROOT.rglob("*.yml")):
        rel = path.relative_to(GRAMMAR_EXPECTATION_ROOT).with_suffix(".check")
        text = path.read_text(encoding="utf-8")
        if EXPECTED_DIAGNOSTIC_RE.search(text):
            invalid.append(
                f"{rel.as_posix()}: expected_diagnostic is not a grammar expectation field"
            )
            continue

        match = EXPECTED_RE.search(text)
        if not match:
            invalid.append(f"{rel.as_posix()}: missing expected field")
            continue

        verdict = match.group(1)
        if verdict not in {"ACCEPT", "REJECT"}:
            invalid.append(f"{rel.as_posix()}: unsupported expected value {verdict}")
            continue

        expectations[rel] = verdict

    return expectations, invalid


def ast_run_verdict(path: Path) -> tuple[str | None, str | None]:
    run_lines = [
        line.strip()
        for line in path.read_text(encoding="utf-8").splitlines()
        if line.strip().startswith("// RUN:")
    ]

    if len(run_lines) != 1:
        return None, f"expected exactly one RUN line, found {len(run_lines)}"

    if run_lines[0].startswith("// RUN: !"):
        return "REJECT", None
    return "ACCEPT", None


def main() -> int:
    corpus_as_checks = {
        path.with_suffix(".check") for path in rel_files(CORPUS_ROOT, ".zom")
    }
    ast_checks = rel_files(AST_EXPECTATION_ROOT, ".check")
    grammar_expectations, invalid_grammar = load_grammar_expectations()

    missing = sorted(corpus_as_checks - ast_checks)
    unexpected = sorted(ast_checks - corpus_as_checks - ALLOWED_EXTRA_AST_CHECKS)
    missing_ast_for_grammar = sorted(set(grammar_expectations) - ast_checks)
    ast_without_grammar = sorted(
        ast_checks
        - set(grammar_expectations)
        - ALLOWED_AST_WITHOUT_GRAMMAR_CHECKS
        - ALLOWED_EXTRA_AST_CHECKS
    )
    stale_ast_only_allowlist = sorted(ALLOWED_AST_WITHOUT_GRAMMAR_CHECKS - ast_checks)

    run_errors: list[str] = []
    verdict_mismatches: list[str] = []
    for rel, expected in sorted(grammar_expectations.items()):
        check_path = AST_EXPECTATION_ROOT / rel
        if not check_path.exists():
            continue

        actual, error = ast_run_verdict(check_path)
        if error:
            run_errors.append(f"{rel.as_posix()}: {error}")
            continue

        if actual != expected:
            verdict_mismatches.append(
                f"{rel.as_posix()}: grammar expects {expected}, AST RUN expects {actual}"
            )

    if (
        missing
        or unexpected
        or invalid_grammar
        or missing_ast_for_grammar
        or ast_without_grammar
        or stale_ast_only_allowlist
        or run_errors
        or verdict_mismatches
    ):
        print("AST conformance coverage check failed.", file=sys.stderr)
        print_examples("Missing AST expectation files", missing)
        print_examples("Unexpected AST expectation files", unexpected)
        print_text_examples("Invalid grammar expectation metadata", invalid_grammar)
        print_examples("Grammar expectations without AST checks", missing_ast_for_grammar)
        print_examples("AST checks without grammar expectation or explicit allowlist", ast_without_grammar)
        print_examples("Stale AST-only allowlist entries", stale_ast_only_allowlist)
        print_text_examples("Invalid AST RUN directives", run_errors)
        print_text_examples("AST RUN verdict mismatches against grammar expectations", verdict_mismatches)
        return 1

    print(
        "AST conformance coverage check passed "
        f"({len(corpus_as_checks)} corpus inputs, "
        f"{len(ast_checks)} AST expectations, "
        f"{len(grammar_expectations)} grammar verdicts, "
        f"{len(ALLOWED_AST_WITHOUT_GRAMMAR_CHECKS)} explicit AST-only checks, "
        f"{len(ALLOWED_EXTRA_AST_CHECKS)} explicit extras)."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
