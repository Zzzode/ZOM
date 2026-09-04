#!/usr/bin/env python3
"""Enforce Binder authority, publication, and producer/verifier boundaries."""

from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BINDER = Path("compiler/binder")
TESTS = Path("tests/unittests/compiler/binder")


RUN_SOURCE = BINDER / "binding-run.cc"
METADATA_HEADER = BINDER / "metadata/binding-metadata.h"
VERIFIER_HEADER = BINDER / "internal/binding-verifier.h"
BUILDER_SOURCE = BINDER / "binding-builder.cc"
CODEC_SOURCE = BINDER / "binding-candidate-codec.cc"
VALIDATOR_SOURCE = BINDER / "binding-candidate-validator.cc"
CAPTURE_VALIDATOR_HEADER = BINDER / "internal/binding-capture-validator.h"
CAPTURE_VALIDATOR_SOURCE = BINDER / "binding-capture-validator.cc"
CONTROL_VALIDATOR_HEADER = BINDER / "internal/binding-control-validator.h"
CONTROL_VALIDATOR_SOURCE = BINDER / "binding-control-validator.cc"
CONTEXT_VALIDATOR_HEADER = BINDER / "internal/binding-context-validator.h"
CONTEXT_VALIDATOR_SOURCE = BINDER / "binding-context-validator.cc"
PUBLICATION_SOURCE = BINDER / "binding-publication.cc"
VERIFIER_SOURCE = BINDER / "binding-verifier.cc"
STABLE_IDENTITY_VERIFIER_SOURCE = BINDER / "stable/candidate/verifier.cc"
STABLE_HEADER_VERIFIER_HEADER = BINDER / "stable/header/verifier.h"
STABLE_HEADER_VERIFIER_SOURCE = BINDER / "stable/header/verifier.cc"
STABLE_HEADER_VERIFIER_TEST = TESTS / "stable/header/verifier-test.cc"
STABLE_BINDING_QUERY_TEST = TESTS / "stable-binding-query-test.cc"
CANONICAL_HEADER_VERIFIER_HEADER = BINDER / "canonical/canonical-header-verifier.h"
CANONICAL_HEADER_VERIFIER_SOURCE = BINDER / "canonical/canonical-header-verifier.cc"
BINDER_CMAKE = BINDER / "CMakeLists.txt"
TEST_CMAKE = TESTS / "CMakeLists.txt"
TEST_SOURCE = TESTS / "binding-input-test.cc"
BODY_BINDING_SOURCE = BINDER / "body-binding.cc"
CLOSURE_FREE_VARIABLES_SOURCE = BINDER / "closure-free-variables.cc"
DRIVER_SESSION_SOURCE = Path("compiler/driver/session/compiler-session.cc")
DRIVER_SESSION_TEST = Path(
    "tests/unittests/compiler/driver/compiler-session-package-test.cc"
)
FACT_SCHEMA = BINDER / "binding-fact-schema.def"
FACT_SCHEMA_GATE = Path("scripts/check-binder-fact-schema.py")
STABLE_SCHEMA = BINDER / "stable-binding-schema.def"
STABLE_FACTS_HEADER = BINDER / "stable/stable-binding-facts.h"
STABLE_FACTS_SOURCE = BINDER / "stable/stable-binding-facts.cc"
STABLE_CODEC_HEADER = BINDER / "stable/stable-binding-codec.h"
STABLE_CODEC_SOURCE = BINDER / "stable/stable-binding-codec.cc"
STABLE_TEST_SOURCE = TESTS / "stable-binding-facts-test.cc"
STABLE_SCHEMA_GATE = Path("scripts/check-stable-binding-schema.py")
LANDING_SCOPE_GATE = Path("scripts/check-landing-scope.py")
STABLE_LANDING_ALLOWLIST = Path(
    "tests/coverage/rfc-0030-stable-binding-landing-files.txt"
)
MODULE_BODY_HEADER = BINDER / "surface/module-body-syntax.h"
MODULE_BODY_VALUE_SOURCE = BINDER / "surface/module-body-syntax.cc"
MODULE_BODY_PRODUCER_SOURCE = BINDER / "surface/module-body-syntax-producer.cc"
MODULE_BODY_VERIFIER_SOURCE = BINDER / "surface/module-body-syntax-verifier.cc"
MODULE_BODY_TEST_SOURCE = TESTS / "module-body-syntax-test.cc"
NAMED_INVENTORY_HEADER = BINDER / "identity/named-identity-inventory.h"
NAMED_INVENTORY_SOURCE = BINDER / "identity/named-identity-inventory.cc"
NAMED_INVENTORY_QUERY_SOURCE = Path(
    "compiler/driver/query/binding/named-identity-inventory-query.cc"
)
NAMED_INVENTORY_QUERY_TEST = Path(
    "tests/unittests/compiler/driver/named-identity-inventory-query-test.cc"
)
INCREMENTAL_BINDING_QUERY_TEST = Path(
    "tests/unittests/compiler/driver/incremental-binding-query-adapter-test.cc"
)

PRODUCTION_COMPONENTS = (
    BUILDER_SOURCE,
    CODEC_SOURCE,
    VALIDATOR_SOURCE,
    CAPTURE_VALIDATOR_SOURCE,
    CONTROL_VALIDATOR_SOURCE,
    CONTEXT_VALIDATOR_SOURCE,
    PUBLICATION_SOURCE,
    VERIFIER_SOURCE,
)
VERIFICATION_COMPONENTS = (
    CODEC_SOURCE,
    VALIDATOR_SOURCE,
    CAPTURE_VALIDATOR_SOURCE,
    CONTROL_VALIDATOR_SOURCE,
    CONTEXT_VALIDATOR_SOURCE,
    VERIFIER_SOURCE,
)
TEST_ORACLES = (
    TESTS / "binding-closure-oracle.cc",
    TESTS / "binding-context-oracle.cc",
    TESTS / "binding-control-oracle.cc",
    TESTS / "binding-differential-oracle.cc",
    TESTS / "binding-explicit-capture-oracle.cc",
)
SEMANTIC_TEST_ORACLES = tuple(
    path for path in TEST_ORACLES if path.name != "binding-differential-oracle.cc"
)
PRODUCER_HEADERS = (
    "binding-skeleton.h",
    "body-binding.h",
    "closure-free-variables.h",
    "control-transfer.h",
    "label-facts.h",
    "scope-arena.h",
)
PRODUCER_SYMBOLS = (
    "BindingBuilder::",
    "BindingSkeletonBuilder::",
    "BodyBindingBuilder::",
    "ClosureFreeVariableBuilder::",
    "ControlTransferBuilder::",
    "LabelBuilder::",
    "ScopeArenaBuilder::",
)
REMOVED_BINDER_FILES = tuple(
    BINDER / name
    for name in (
        "binder.cc",
        "binder.h",
        "decl-collector.cc",
        "decl-collector.h",
        "definition-identity-map.cc",
        "definition-identity-map.h",
        "import-resolver.cc",
        "import-resolver.h",
        "name-resolver.cc",
        "name-resolver.h",
        "utilities.cc",
        "utilities.h",
    )
)
REMOVED_SYMBOL_ROOT = Path("compiler/symbol")
SCHEMA_INCLUDE = '#include "compiler/binder/binding-fact-schema.def"'


def source_files() -> dict[Path, str]:
    files: dict[Path, str] = {}
    for relative_root in (Path("compiler"), Path("tests")):
        for directory, child_directories, names in os.walk(ROOT / relative_root):
            child_directories[:] = [
                name
                for name in child_directories
                if name not in {".antlr_build", "Output", "Testing", "__pycache__", "vendor"}
            ]
            for name in names:
                path = Path(directory) / name
                if path.suffix not in {".cc", ".def", ".h"} and name != "CMakeLists.txt":
                    continue
                relative = path.relative_to(ROOT)
                files[relative] = path.read_text(encoding="utf-8")
    for required in required_files():
        absolute = ROOT / required
        if absolute.exists():
            files.setdefault(required, absolute.read_text(encoding="utf-8"))
    return files


def required_files() -> tuple[Path, ...]:
    return (
        RUN_SOURCE,
        METADATA_HEADER,
        VERIFIER_HEADER,
        CAPTURE_VALIDATOR_HEADER,
        CONTROL_VALIDATOR_HEADER,
        CONTEXT_VALIDATOR_HEADER,
        *PRODUCTION_COMPONENTS,
        *TEST_ORACLES,
        BINDER_CMAKE,
        TEST_CMAKE,
        TEST_SOURCE,
        BODY_BINDING_SOURCE,
        CLOSURE_FREE_VARIABLES_SOURCE,
        FACT_SCHEMA,
        FACT_SCHEMA_GATE,
        STABLE_SCHEMA,
        STABLE_FACTS_HEADER,
        STABLE_FACTS_SOURCE,
        STABLE_CODEC_HEADER,
        STABLE_CODEC_SOURCE,
        STABLE_TEST_SOURCE,
        STABLE_SCHEMA_GATE,
        LANDING_SCOPE_GATE,
        STABLE_LANDING_ALLOWLIST,
        STABLE_IDENTITY_VERIFIER_SOURCE,
        STABLE_HEADER_VERIFIER_HEADER,
        STABLE_HEADER_VERIFIER_SOURCE,
        STABLE_HEADER_VERIFIER_TEST,
        STABLE_BINDING_QUERY_TEST,
        CANONICAL_HEADER_VERIFIER_HEADER,
        CANONICAL_HEADER_VERIFIER_SOURCE,
        DRIVER_SESSION_SOURCE,
        DRIVER_SESSION_TEST,
        MODULE_BODY_HEADER,
        MODULE_BODY_VALUE_SOURCE,
        MODULE_BODY_PRODUCER_SOURCE,
        MODULE_BODY_VERIFIER_SOURCE,
        MODULE_BODY_TEST_SOURCE,
        NAMED_INVENTORY_HEADER,
        NAMED_INVENTORY_SOURCE,
        NAMED_INVENTORY_QUERY_SOURCE,
        NAMED_INVENTORY_QUERY_TEST,
        INCREMENTAL_BINDING_QUERY_TEST,
    )


CURRENT_BINDER_COMPONENTS = (
    "canonical/canonical-input-payload-digest.cc",
    "metadata/immutable-binding-metadata.cc",
    "metadata/immutable-definition-inventory.cc",
    "graph/materialized-module-skeleton.cc",
    "surface/materialized-export-surface-verifier.cc",
    "graph/module-binding-allocation-plan.cc",
    "graph/module-graph-revision.cc",
    "graph/module-graph-source-failure.cc",
    "surface/owner-body-query.cc",
    "stable/candidate/producer.cc",
    "stable/candidate/verifier.cc",
)

REMOVED_BATCH_COMPONENTS = (
    "binding-builder.cc",
    "binding-candidate-codec.cc",
    "binding-candidate-validator.cc",
    "binding-capture-validator.cc",
    "binding-context-validator.cc",
    "binding-control-validator.cc",
    "binding-input.cc",
    "binding-publication.cc",
    "binding-run.cc",
    "binding-verifier.cc",
    "frozen-definition-inventory.cc",
    "verified-bound-module-input.cc",
)


def current_check(files: dict[Path, str]) -> list[str]:
    errors: list[str] = []
    cmake = files.get(BINDER_CMAKE, "")
    test_cmake = files.get(TEST_CMAKE, "")

    for component in CURRENT_BINDER_COMPONENTS:
        path = BINDER / component
        if path not in files:
            errors.append(f"{path}: current Binder component is missing")
        if f"${{CMAKE_CURRENT_SOURCE_DIR}}/{component}" not in cmake:
            errors.append(f"{BINDER_CMAKE}: current Binder component is omitted: {component}")
    for component in REMOVED_BATCH_COMPONENTS:
        path = BINDER / component
        if path in files:
            errors.append(f"{path}: removed batch Binder component remains")
        if component in cmake:
            errors.append(f"{BINDER_CMAKE}: removed batch Binder component remains: {component}")

    for marker in (
        "class CandidateProducer final",
        "class CandidateVerifier final",
        "class MaterializedModuleSkeleton final",
        "class ImmutableBindingMetadata final",
        "class ImmutableDefinitionInventory final",
    ):
        if not any(marker in text for text in files.values()):
            errors.append(f"current Binder architecture is missing: {marker}")
    for marker in (
        'add_ztest_unit_test("materialized-module-skeleton-test"',
        'add_ztest_unit_test("owner-body-syntax-traversal-test"',
        'add_ztest_unit_test("stable-binding-diagnostic-fact-test"',
    ):
        if marker not in test_cmake:
            errors.append(f"{TEST_CMAKE}: current Binder test is omitted: {marker}")
    for path, text in files.items():
        if path.suffix not in {".cc", ".h"}:
            continue
        if "SemanticIdentityRegistrySet" in text or "FrozenDefinitionInventory" in text:
            errors.append(f"{path}: removed Binder authority remains")
    return errors


def current_self_test(files: dict[Path, str]) -> list[str]:
    cases = (
        (
            BINDER_CMAKE,
            "${CMAKE_CURRENT_SOURCE_DIR}/graph/materialized-module-skeleton.cc",
            "${CMAKE_CURRENT_SOURCE_DIR}/missing-materialized-module-skeleton.cc",
            "current Binder component is omitted",
        ),
        (
            BINDER_CMAKE,
            "${CMAKE_CURRENT_SOURCE_DIR}/stable/candidate/verifier.cc",
            "${CMAKE_CURRENT_SOURCE_DIR}/binding-verifier.cc",
            "removed batch Binder component remains",
        ),
        (
            BINDER / "graph/materialized-module-skeleton.cc",
            "MaterializedModuleSkeleton",
            "FrozenDefinitionInventory",
            "removed Binder authority remains",
        ),
    )
    errors: list[str] = []
    for path, original, replacement, expected in cases:
        mutated = dict(files)
        text = mutated.get(path, "")
        if original not in text:
            errors.append(f"self-test fixture drifted: {path}: {original}")
            continue
        mutated[path] = text.replace(original, replacement, 1)
        if not any(expected in error for error in current_check(mutated)):
            errors.append(f"self-test mutation escaped: {path}: {original}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    files = source_files()
    errors = current_check(files) if args.check else current_self_test(files)
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    if args.check:
        print("binder architecture check passed")
    else:
        print("binder architecture self-test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
