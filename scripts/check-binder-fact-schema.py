#!/usr/bin/env python3
"""Validate the Binder fact schema, generated use sites, and mutation inventory."""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
BINDER = ROOT / "products/zomlang/compiler/binder"
TESTS = ROOT / "products/zomlang/tests/unittests/compiler/binder"
SCHEMA = BINDER / "binding-fact-schema.def"

KNOWN_DOMAINS = {
    "Closure",
    "Context",
    "Control",
    "Definition",
    "Diagnostics",
    "ImportExport",
    "Resolution",
    "Scope",
}
KNOWN_MUTATIONS = {
    "additional",
    "duplicate",
    "enum",
    "field",
    "identity",
    "missing",
    "relation",
    "reordered",
    "source",
}
REQUIRED_COLLECTION_MUTATIONS = {"additional", "missing", "reordered"}
PRODUCER_HEADERS = {
    "binding-skeleton.h",
    "body-binding.h",
    "closure-free-variables.h",
    "control-transfer.h",
    "label-facts.h",
    "scope-arena.h",
}
PRODUCTION_VERIFICATION_SOURCES = (
    "binding-candidate-codec.cc",
    "binding-candidate-validator.cc",
    "binding-definition-fact-validator.cc",
    "binding-verifier.cc",
)
PRODUCTION_SOURCES = (
    "binding-builder.cc",
    "binding-candidate-codec.cc",
    "binding-candidate-validator.cc",
    "binding-definition-fact-validator.cc",
    "binding-publication.cc",
    "binding-verifier.cc",
)
TEST_ORACLE_SOURCES = (
    "binding-closure-oracle.cc",
    "binding-context-oracle.cc",
    "binding-control-oracle.cc",
    "binding-differential-oracle.cc",
    "binding-explicit-capture-oracle.cc",
)


@dataclass(frozen=True)
class Fact:
    identifier: str
    record_type: str
    member: str
    accessor: str
    publication: str
    tag: int
    domain: str
    mutations: frozenset[str]
    test: str


def parse_schema(text: str) -> tuple[list[Fact], list[str]]:
    errors: list[str] = []
    facts: list[Fact] = []
    for match in re.finditer(r"ZOM_BINDING_FACT\((.*?)\)\s*", text, re.DOTALL):
        columns = [column.strip() for column in match.group(1).split(",")]
        if len(columns) != 9:
            errors.append(f"schema entry has {len(columns)} columns instead of 9")
            continue
        identifier, record_type, member, accessor, publication, tag_text, domain = columns[:7]
        mutations_text, test_text = columns[7:]
        if not (mutations_text.startswith('"') and mutations_text.endswith('"')):
            errors.append(f"{identifier}: mutation inventory must be one quoted string")
            continue
        if not (test_text.startswith('"') and test_text.endswith('"')):
            errors.append(f"{identifier}: mutation test must be one quoted string")
            continue
        try:
            tag = int(tag_text, 0)
        except ValueError:
            errors.append(f"{identifier}: invalid stable tag {tag_text}")
            continue
        facts.append(
            Fact(
                identifier=identifier,
                record_type=record_type,
                member=member,
                accessor=accessor,
                publication=publication,
                tag=tag,
                domain=domain,
                mutations=frozenset(mutations_text[1:-1].split("|")),
                test=test_text[1:-1],
            )
        )
    if not facts:
        errors.append("schema contains no fact entries")
    return facts, errors


def duplicates(values: list[object]) -> set[object]:
    return {value for value in values if values.count(value) != 1}


def check() -> list[str]:
    facts, errors = parse_schema(SCHEMA.read_text(encoding="utf-8"))
    for label, values in (
        ("identifier", [fact.identifier for fact in facts]),
        ("record type", [fact.record_type for fact in facts]),
        ("candidate member", [fact.member for fact in facts]),
        ("stable tag", [fact.tag for fact in facts]),
    ):
        for value in sorted(duplicates(values), key=str):
            errors.append(f"duplicate {label}: {value}")

    expected_tags = list(range(1, len(facts) + 1))
    if [fact.tag for fact in facts] != expected_tags:
        errors.append("stable tags must be dense and schema ordered from 0x01")

    test_source = (TESTS / "binding-input-test.cc").read_text(encoding="utf-8")
    codec_source = (BINDER / "binding-candidate-codec.cc").read_text(encoding="utf-8")
    for fact in facts:
        if fact.publication not in {"Internal", "Published"}:
            errors.append(f"{fact.identifier}: invalid publication {fact.publication}")
        if fact.domain not in KNOWN_DOMAINS:
            errors.append(f"{fact.identifier}: unknown domain {fact.domain}")
        unknown_mutations = fact.mutations - KNOWN_MUTATIONS
        if unknown_mutations:
            errors.append(
                f"{fact.identifier}: unknown mutations {','.join(sorted(unknown_mutations))}"
            )
        missing_mutations = REQUIRED_COLLECTION_MUTATIONS - fact.mutations
        if missing_mutations:
            errors.append(
                f"{fact.identifier}: missing collection mutations "
                f"{','.join(sorted(missing_mutations))}"
            )
        if f'ZC_TEST("{fact.test}")' not in test_source:
            errors.append(f"{fact.identifier}: mutation test is absent: {fact.test}")
        codec_pattern = re.compile(
            r"const\s+" + re.escape(fact.record_type) + r"&\s+fact\)", re.DOTALL
        )
        if not codec_pattern.search(codec_source):
            errors.append(f"{fact.identifier}: canonical record codec is absent")

    verifier_header = (BINDER / "internal/binding-verifier.h").read_text(encoding="utf-8")
    metadata_header = (BINDER / "binding-metadata.h").read_text(encoding="utf-8")
    publication_source = (BINDER / "binding-publication.cc").read_text(encoding="utf-8")
    differential_source = (TESTS / "binding-differential-oracle.cc").read_text(encoding="utf-8")
    required_schema_users = {
        "candidate record": verifier_header,
        "published accessors": metadata_header,
        "accessor implementation": publication_source,
        "canonical sequence codec": codec_source,
        "differential count inventory": differential_source,
    }
    for label, source in required_schema_users.items():
        if '#include "zomlang/compiler/binder/binding-fact-schema.def"' not in source:
            errors.append(f"{label} does not consume binding-fact-schema.def")

    for source_name in PRODUCTION_VERIFICATION_SOURCES:
        source = (BINDER / source_name).read_text(encoding="utf-8")
        if "BindingBuilder::" in source:
            errors.append(f"{source_name}: production verification reuses BindingBuilder")
        for header in PRODUCER_HEADERS:
            if header in source:
                errors.append(f"{source_name}: production verification includes {header}")

    binder_cmake = (BINDER / "CMakeLists.txt").read_text(encoding="utf-8")
    for source_name in PRODUCTION_SOURCES:
        if source_name not in binder_cmake:
            errors.append(f"Binder CMake omits {source_name}")
    test_cmake = (TESTS / "CMakeLists.txt").read_text(encoding="utf-8")
    for source_name in TEST_ORACLE_SOURCES:
        if source_name not in test_cmake:
            errors.append(f"Binder test CMake omits {source_name}")
        if source_name in binder_cmake:
            errors.append(f"test-only oracle leaked into production Binder: {source_name}")

    line_limits = {
        "binding-verifier.cc": 300,
        "binding-builder.cc": 1000,
        "binding-candidate-codec.cc": 1000,
        "binding-candidate-validator.cc": 1800,
        "binding-definition-fact-validator.cc": 300,
        "binding-publication.cc": 400,
    }
    for source_name, limit in line_limits.items():
        count = len((BINDER / source_name).read_text(encoding="utf-8").splitlines())
        if count > limit:
            errors.append(f"{source_name}: {count} lines exceeds domain limit {limit}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="validate the live repository")
    args = parser.parse_args()
    if not args.check:
        parser.error("--check is required")
    errors = check()
    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1
    print("binder fact schema: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
