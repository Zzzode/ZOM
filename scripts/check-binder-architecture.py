#!/usr/bin/env python3
"""Enforce the dependency-free RFC 0004 binding-input architecture slice."""

from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BINDER_DIR = Path("products/zomlang/compiler/binder")
HEADER = BINDER_DIR / "binding-input.h"
SOURCE = BINDER_DIR / "binding-input.cc"
PARSED_HEADER = BINDER_DIR / "parsed-module.h"
PARSED_SOURCE = BINDER_DIR / "parsed-module.cc"
INVENTORY_HEADER = BINDER_DIR / "frozen-definition-inventory.h"
INVENTORY_SOURCE = BINDER_DIR / "frozen-definition-inventory.cc"
BINDER_CMAKE = BINDER_DIR / "CMakeLists.txt"
TEST_DIR = Path("products/zomlang/tests/unittests/compiler/binder")
TEST_SOURCE = TEST_DIR / "binding-input-test.cc"
TEST_CMAKE = TEST_DIR / "CMakeLists.txt"
FORBIDDEN_INCLUDE_ROOTS = (
    Path("products/zomlang/compiler/checker"),
    Path("products/zomlang/compiler/irgen"),
    Path("products/zomlang/compiler/symbol"),
)


def production_files() -> dict[Path, str]:
    files: dict[Path, str] = {}
    roots = (Path("products/zomlang/compiler"), Path("products/zomlang/tests"))
    for root in roots:
        for directory, child_directories, names in os.walk(ROOT / root):
            child_directories[:] = [
                name
                for name in child_directories
                if name not in {"Output", "Testing", ".antlr_build", "__pycache__", "vendor"}
            ]
            for name in names:
                path = Path(directory) / name
                if path.suffix not in {".h", ".cc"} and name != "CMakeLists.txt":
                    continue
                relative = path.relative_to(ROOT)
                files[relative] = path.read_text(encoding="utf-8")
    for required in (
        HEADER,
        SOURCE,
        PARSED_HEADER,
        PARSED_SOURCE,
        INVENTORY_HEADER,
        INVENTORY_SOURCE,
        BINDER_CMAKE,
        TEST_SOURCE,
        TEST_CMAKE,
    ):
        files.setdefault(required, (ROOT / required).read_text(encoding="utf-8"))
    return files


def type_body(text: str, name: str) -> str:
    match = re.search(rf"\b(?:class|struct)\s+{re.escape(name)}\s+final\s*\{{", text)
    if match is None:
        return ""
    start = match.end()
    depth = 1
    for index in range(start, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start:index]
    return ""


def check_private_verified_constructors(files: dict[Path, str], errors: list[str]) -> None:
    types = (
        (HEADER, "VerifiedModuleGraphView"),
        (HEADER, "VerifiedBindingInput"),
        (PARSED_HEADER, "UnbrandedParsedModule"),
        (PARSED_HEADER, "VerifiedParsedModule"),
        (INVENTORY_HEADER, "FrozenDefinitionInventoryView"),
    )
    for header_path, name in types:
        body = type_body(files.get(header_path, ""), name)
        private = body.find("private:")
        constructor = f"explicit {name}(zc::Own<Impl>&& impl) noexcept;"
        if private < 0 or body.find(constructor, private) < 0:
            errors.append(f"{header_path}: {name} verified constructor must be private")
        public = body[:private] if private >= 0 else body
        for match in re.finditer(rf"(?<!~)\b{name}\s*\(([^)]*)\)", public):
            if f"{name}&&" not in match.group(1):
                errors.append(f"{header_path}: {name} exposes a non-move public constructor")


def check_unique_construction(files: dict[Path, str], errors: list[str]) -> None:
    publication_sources = {
        "VerifiedModuleGraphView": SOURCE,
        "VerifiedBindingInput": SOURCE,
        "UnbrandedParsedModule": PARSED_SOURCE,
        "VerifiedParsedModule": PARSED_SOURCE,
        "FrozenDefinitionInventoryView": INVENTORY_SOURCE,
    }
    for path, text in files.items():
        if path.suffix not in {".h", ".cc"} or TEST_DIR in path.parents:
            continue
        for name, publication_source in publication_sources.items():
            if path in {HEADER, PARSED_HEADER, INVENTORY_HEADER}:
                continue
            if path != publication_source and re.search(rf"\b{name}\s*\(", text):
                errors.append(f"{path}: {name} may only be constructed in {publication_source}")
            impl_marker = f"zc::heap<{name}::Impl>"
            if path != publication_source and impl_marker in text:
                errors.append(f"{path}: {name} private implementation construction escaped")
    for name, publication_source in publication_sources.items():
        if files.get(publication_source, "").count(f"zc::heap<{name}::Impl>") != 1:
            errors.append(f"{publication_source}: {name} must have exactly one publication site")


def check_verified_input_surface(files: dict[Path, str], errors: list[str]) -> None:
    candidate = type_body(files.get(HEADER, ""), "BindingInputCandidate")
    for forbidden in ("ast::Tree", "DefinitionIdentityMap", "SourceFileId source"):
        if forbidden in candidate:
            errors.append(f"{HEADER}: BindingInputCandidate exposes raw input: {forbidden}")
    for required in ("VerifiedParsedModule", "FrozenDefinitionInventoryView"):
        if required not in candidate:
            errors.append(f"{HEADER}: BindingInputCandidate is missing {required}")


def check_producer_boundaries(files: dict[Path, str], errors: list[str]) -> None:
    allowed_admission = {
        PARSED_SOURCE,
        Path("products/zomlang/compiler/basic/frontend.cc"),
    }
    allowed_inventory = {
        INVENTORY_SOURCE,
        Path("products/zomlang/compiler/driver/compiler-session.cc"),
    }
    for path, text in files.items():
        if TEST_DIR in path.parents:
            continue
        if "ParsedModuleVerifier::admit(" in text and path not in allowed_admission:
            errors.append(f"{path}: parsed-module admission escaped the parse driver boundary")
        if (
            "FrozenDefinitionInventoryVerifier::verifySingleModule(" in text
            and path not in allowed_inventory
        ):
            errors.append(f"{path}: frozen inventory publication escaped the session collector boundary")


def check_layering(files: dict[Path, str], errors: list[str]) -> None:
    include = '"zomlang/compiler/binder/binding-input.h"'
    for path, text in files.items():
        if any(root == path or root in path.parents for root in FORBIDDEN_INCLUDE_ROOTS):
            if include in text:
                errors.append(f"{path}: checker/irgen/symbol cannot include binding-input.h")
        if "BindingInputCandidate" not in text or path == HEADER:
            continue
        allowed = BINDER_DIR in path.parents or Path("products/zomlang/compiler/driver") in path.parents
        allowed = allowed or Path("products/zomlang/tests") in path.parents
        if not allowed:
            errors.append(f"{path}: BindingInputCandidate escaped binder/driver/tests")


def check_wiring(files: dict[Path, str], errors: list[str]) -> None:
    required = (
        (BINDER_CMAKE, "${CMAKE_CURRENT_SOURCE_DIR}/binding-input.cc"),
        (BINDER_CMAKE, "${CMAKE_CURRENT_SOURCE_DIR}/parsed-module.cc"),
        (BINDER_CMAKE, "${CMAKE_CURRENT_SOURCE_DIR}/frozen-definition-inventory.cc"),
        (TEST_CMAKE, 'add_ztest_unit_test("binding-input-test" "binding-input-test.cc"'),
        (TEST_CMAKE, "binder-architecture"),
        (TEST_CMAKE, "check-binder-architecture.py --check"),
        (TEST_CMAKE, "check-binder-architecture.py --self-test"),
    )
    for path, marker in required:
        if marker not in files.get(path, ""):
            errors.append(f"{path}: missing architecture wiring marker: {marker}")
    if SOURCE not in files or TEST_SOURCE not in files:
        errors.append("binding-input production source and focused test must both exist")


def check_no_compatibility_facade(files: dict[Path, str], errors: list[str]) -> None:
    binder_header = files.get(BINDER_DIR / "binder.h", "")
    if "VerifiedBindingInput" in binder_header or "BindingInputCandidate" in binder_header:
        errors.append(f"{BINDER_DIR / 'binder.h'}: raw Binder compatibility facade is forbidden")
    for path in (HEADER, SOURCE):
        text = files.get(path, "")
        if '"zomlang/compiler/binder/binder.h"' in text or re.search(r"\bBinder\s*\(", text):
            errors.append(f"{path}: verified input cannot wrap or call the old Binder")


def check(files: dict[Path, str]) -> list[str]:
    errors: list[str] = []
    check_private_verified_constructors(files, errors)
    check_unique_construction(files, errors)
    check_verified_input_surface(files, errors)
    check_producer_boundaries(files, errors)
    check_layering(files, errors)
    check_wiring(files, errors)
    check_no_compatibility_facade(files, errors)
    return sorted(set(errors))


def self_test(files: dict[Path, str]) -> list[str]:
    cases: tuple[tuple[str, Path, str, str], ...] = (
        ("public constructor", HEADER, "class VerifiedBindingInput final {\npublic:", "class VerifiedBindingInput final {\npublic:\n  explicit VerifiedBindingInput(int);"),
        ("foreign construction", Path("products/zomlang/compiler/checker/escape.cc"), "", "VerifiedBindingInput(value);"),
        ("forbidden include", Path("products/zomlang/compiler/irgen/escape.cc"), "", '#include "zomlang/compiler/binder/binding-input.h"'),
        ("candidate escape", Path("products/zomlang/compiler/lexer/escape.cc"), "", "BindingInputCandidate escaped;"),
        ("missing source wiring", BINDER_CMAKE, "${CMAKE_CURRENT_SOURCE_DIR}/binding-input.cc", "${CMAKE_CURRENT_SOURCE_DIR}/missing.cc"),
        ("compatibility facade", BINDER_DIR / "binder.h", "", "\nVerifiedBindingInput Binder(BindingInputCandidate);\n"),
        (
            "raw tree candidate",
            HEADER,
            "const VerifiedParsedModule& parsedModule;",
            "const ast::Tree& parsedModule;",
        ),
        (
            "raw definition candidate",
            HEADER,
            "const FrozenDefinitionInventoryView& definitions;",
            "const DefinitionIdentityMap& definitions;",
        ),
        (
            "public parsed constructor",
            PARSED_HEADER,
            "class VerifiedParsedModule final {\npublic:",
            "class VerifiedParsedModule final {\npublic:\n  explicit VerifiedParsedModule(int);",
        ),
        (
            "foreign parsed publication",
            Path("products/zomlang/compiler/parser/escape.cc"),
            "",
            "VerifiedParsedModule(value);",
        ),
        (
            "public inventory constructor",
            INVENTORY_HEADER,
            "class FrozenDefinitionInventoryView final {\npublic:",
            "class FrozenDefinitionInventoryView final {\npublic:\n  explicit FrozenDefinitionInventoryView(int);",
        ),
        (
            "foreign parser admission",
            Path("products/zomlang/compiler/parser/escape.cc"),
            "",
            "ParsedModuleVerifier::admit(snapshot, sources, buffer, tree);",
        ),
        (
            "foreign inventory publication",
            Path("products/zomlang/compiler/binder/escape.cc"),
            "",
            "FrozenDefinitionInventoryVerifier::verifySingleModule(context, module, parsed, registries, definitions);",
        ),
    )
    failures: list[str] = []
    for label, path, old, new in cases:
        mutated = dict(files)
        original = mutated.get(path, "")
        if old and old not in original:
            failures.append(f"self-test fixture drifted: {label}")
            continue
        mutated[path] = original.replace(old, new, 1) if old else original + new
        if not check(mutated):
            failures.append(f"self-test mutation escaped: {label}")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    files = production_files()
    errors = check(files) if args.check else self_test(files)
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print("binder architecture check passed" if args.check else "binder architecture self-test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
