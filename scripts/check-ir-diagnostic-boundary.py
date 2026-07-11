#!/usr/bin/env python3

import argparse
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
IRGEN_ROOT = ROOT / "products" / "zomlang" / "compiler" / "irgen"
LOWERING_HEADER = Path("products/zomlang/compiler/irgen/lowering.h")
IR_DUMP_HEADER = Path("products/zomlang/compiler/irgen/ir-dump.h")
CLI_SOURCE = Path("products/zomlang/utils/zomc/zomc.cc")
DIAGNOSTIC_DEFS = Path("products/zomlang/compiler/diagnostics/diagnostics-lowering.def")

REQUIRED_DIAGNOSTICS = {
    "IrUnsupportedSourceShape",
    "IrUnsupportedExpression",
    "IrUnknownTargetLayout",
    "IrCrossSourceCallUnsupported",
    "IrSingleSourceRequired",
    "PanicUnwindUnsupported",
    "BinaryEmissionUnavailable",
    "IrOutputCreationFailed",
    "IrLoweringInvariantViolation",
    "IrCheckedInputMissing",
    "IrDumpInvariantViolation",
}

BANNED_IRGEN_PATTERNS = {
    r"\bZC_IREQUIRE\b": "raw invariant assertion",
    r"\bZC_FAIL(?:_ASSERT)?\b": "raw failure assertion",
    r"\bthrow\b": "exception-based failure",
    r"\bfprintf\s*\(": "direct stderr output",
    r"\bprintf\s*\(": "direct stdout output",
    r"\bstd::cerr\b": "direct stderr output",
    r"\bLoweringError\b": "string-era LoweringError surface",
    r"\b(?:message|errorMessage)\s*;": "untyped failure message field",
    r"IR lowering failed": "ad hoc lowering error prefix",
}


def load_files() -> dict[Path, str]:
    files = {
        path.relative_to(ROOT): path.read_text(encoding="utf-8")
        for suffix in ("*.h", "*.cc")
        for path in IRGEN_ROOT.rglob(suffix)
    }
    for path in (CLI_SOURCE, DIAGNOSTIC_DEFS):
        files[path] = (ROOT / path).read_text(encoding="utf-8")
    return files


def enum_variants(text: str, enum_name: str) -> list[str]:
    match = re.search(rf"enum\s+class\s+{re.escape(enum_name)}\b[^{{]*\{{(.*?)\}};", text, re.S)
    if match is None:
        return []
    variants: list[str] = []
    for item in match.group(1).split(","):
        name = item.split("=", 1)[0].strip()
        if re.fullmatch(r"[A-Za-z_]\w*", name):
            variants.append(name)
    return variants


def function_body(text: str, signature: str) -> str:
    start = text.find(signature)
    if start < 0:
        return ""
    open_brace = text.find("{", start)
    if open_brace < 0:
        return ""
    depth = 0
    for index in range(open_brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[open_brace + 1 : index]
    return ""


def require_exhaustive_cases(
    errors: list[str], path: Path, body: str, enum_name: str, variants: list[str]
) -> None:
    if not body:
        errors.append(f"{path}: missing mapping function for {enum_name}")
        return
    for variant in variants:
        occurrences = len(re.findall(rf"\bcase\s+irgen::{enum_name}::{variant}\s*:", body))
        if occurrences != 1:
            errors.append(
                f"{path}: {enum_name}::{variant} must have exactly one mapping case, found {occurrences}"
            )


def check_irgen_failures(files: dict[Path, str], errors: list[str]) -> None:
    for path, text in sorted(files.items()):
        if not path.is_relative_to(Path("products/zomlang/compiler/irgen")):
            continue
        for pattern, label in BANNED_IRGEN_PATTERNS.items():
            if re.search(pattern, text):
                errors.append(f"{path}: forbidden {label}")

    lowering = files.get(LOWERING_HEADER, "")
    if "using LoweringResult = zc::OneOf<Module, LoweringFailure>;" not in lowering:
        errors.append(f"{LOWERING_HEADER}: lowering must return typed Module-or-failure results")
    if "LoweringFailureKind kind" not in lowering or "LoweringPhase phase" not in lowering:
        errors.append(f"{LOWERING_HEADER}: lowering failures must retain kind and phase")

    dump = files.get(IR_DUMP_HEADER, "")
    if "using IrDumpResult = zc::Maybe<IrDumpFailure>;" not in dump:
        errors.append(f"{IR_DUMP_HEADER}: IR dump must return typed verifier failures")
    if "IrDumpFailureKind kind" not in dump or "IrDumpVerifierSite site" not in dump:
        errors.append(f"{IR_DUMP_HEADER}: dump failures must retain kind and verifier site")


def check_cli_mapping(files: dict[Path, str], errors: list[str]) -> None:
    lowering = files.get(LOWERING_HEADER, "")
    dump = files.get(IR_DUMP_HEADER, "")
    cli = files.get(CLI_SOURCE, "")

    lowering_kinds = enum_variants(lowering, "LoweringFailureKind")
    lowering_phases = enum_variants(lowering, "LoweringPhase")
    dump_kinds = enum_variants(dump, "IrDumpFailureKind")
    dump_sites = enum_variants(dump, "IrDumpVerifierSite")
    for name, variants, path in (
        ("LoweringFailureKind", lowering_kinds, LOWERING_HEADER),
        ("LoweringPhase", lowering_phases, LOWERING_HEADER),
        ("IrDumpFailureKind", dump_kinds, IR_DUMP_HEADER),
        ("IrDumpVerifierSite", dump_sites, IR_DUMP_HEADER),
    ):
        if not variants:
            errors.append(f"{path}: missing or empty {name}")

    require_exhaustive_cases(
        errors,
        CLI_SOURCE,
        function_body(cli, "static zc::StringPtr loweringFailureName("),
        "LoweringFailureKind",
        lowering_kinds,
    )
    require_exhaustive_cases(
        errors,
        CLI_SOURCE,
        function_body(cli, "static zc::StringPtr loweringPhaseName("),
        "LoweringPhase",
        lowering_phases,
    )
    require_exhaustive_cases(
        errors,
        CLI_SOURCE,
        function_body(cli, "static zc::StringPtr irDumpFailureName("),
        "IrDumpFailureKind",
        dump_kinds,
    )
    require_exhaustive_cases(
        errors,
        CLI_SOURCE,
        function_body(cli, "static zc::StringPtr irDumpVerifierSiteName("),
        "IrDumpVerifierSite",
        dump_sites,
    )
    require_exhaustive_cases(
        errors,
        CLI_SOURCE,
        function_body(cli, "zc::MainBuilder::Validity diagnoseLoweringFailure("),
        "LoweringFailureKind",
        lowering_kinds,
    )

    if "DiagID::IrLoweringInvariantViolation" not in cli:
        errors.append(f"{CLI_SOURCE}: missing registered lowering invariant mapping")
    if "DiagID::IrDumpInvariantViolation" not in cli:
        errors.append(f"{CLI_SOURCE}: missing registered dump invariant mapping")


def check_registry(files: dict[Path, str], errors: list[str]) -> None:
    definitions = files.get(DIAGNOSTIC_DEFS, "")
    registered = set(re.findall(r"DIAG\(\d+,\s*([A-Za-z_]\w*)", definitions))
    for name in sorted(REQUIRED_DIAGNOSTICS - registered):
        errors.append(f"{DIAGNOSTIC_DEFS}: missing required registered diagnostic {name}")


def analyze(files: dict[Path, str]) -> list[str]:
    errors: list[str] = []
    check_irgen_failures(files, errors)
    check_cli_mapping(files, errors)
    check_registry(files, errors)
    return errors


def run_check() -> int:
    errors = analyze(load_files())
    if errors:
        print("IR diagnostic boundary check failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    print("IR diagnostic boundary check passed (typed facts and registered mappings).")
    return 0


def expect_rejection(
    baseline: dict[Path, str], name: str, mutate, expected_fragment: str
) -> list[str]:
    fixture = dict(baseline)
    mutate(fixture)
    errors = analyze(fixture)
    if any(expected_fragment in error for error in errors):
        return []
    return [f"negative fixture {name!r} was not rejected for {expected_fragment!r}"]


def run_self_test() -> int:
    baseline = load_files()
    errors = analyze(baseline)
    if errors:
        print("IR diagnostic self-test baseline failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    lowering_source = Path("products/zomlang/compiler/irgen/lowering.cc")
    failures: list[str] = []
    failures += expect_rejection(
        baseline,
        "raw assertion",
        lambda files: files.__setitem__(
            lowering_source, files[lowering_source] + "\nZC_IREQUIRE(false, \"bad\");\n"
        ),
        "raw invariant assertion",
    )
    failures += expect_rejection(
        baseline,
        "unmapped failure kind",
        lambda files: files.__setitem__(
            LOWERING_HEADER,
            files[LOWERING_HEADER].replace(
                "DuplicateFunctionSymbol,", "DuplicateFunctionSymbol,\n  InjectedFailure,"
            ),
        ),
        "LoweringFailureKind::InjectedFailure must have exactly one mapping case",
    )
    failures += expect_rejection(
        baseline,
        "string failure field",
        lambda files: files.__setitem__(
            LOWERING_HEADER,
            files[LOWERING_HEADER].replace(
                "LoweringPhase phase", "zc::String message;\n  LoweringPhase phase"
            ),
        ),
        "untyped failure message field",
    )
    failures += expect_rejection(
        baseline,
        "missing diagnostic definition",
        lambda files: files.__setitem__(
            DIAGNOSTIC_DEFS,
            re.sub(
                r"^DIAG\(6001, IrUnsupportedSourceShape,.*?\n",
                "",
                files[DIAGNOSTIC_DEFS],
                count=1,
                flags=re.M,
            ),
        ),
        "missing required registered diagnostic IrUnsupportedSourceShape",
    )

    if failures:
        print("IR diagnostic self-test failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print("IR diagnostic boundary negative fixtures passed (4/4).")
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
