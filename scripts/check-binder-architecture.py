#!/usr/bin/env python3
"""Enforce Binder authority, publication, and producer/verifier boundaries."""

from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BINDER = Path("products/zomlang/compiler/binder")
TESTS = Path("products/zomlang/tests/unittests/compiler/binder")


def binder_source(path: Path) -> str:
    return str(path.relative_to(BINDER))

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
DRIVER_SESSION_SOURCE = Path("products/zomlang/compiler/driver/session/compiler-session.cc")
DRIVER_SESSION_TEST = Path(
    "products/zomlang/tests/unittests/compiler/driver/compiler-session-package-test.cc"
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
    "products/zomlang/tests/coverage/rfc-0030-stable-binding-landing-files.txt"
)
MODULE_BODY_HEADER = BINDER / "surface/module-body-syntax.h"
MODULE_BODY_VALUE_SOURCE = BINDER / "surface/module-body-syntax.cc"
MODULE_BODY_PRODUCER_SOURCE = BINDER / "surface/module-body-syntax-producer.cc"
MODULE_BODY_VERIFIER_SOURCE = BINDER / "surface/module-body-syntax-verifier.cc"
MODULE_BODY_TEST_SOURCE = TESTS / "module-body-syntax-test.cc"
NAMED_INVENTORY_HEADER = BINDER / "identity/named-identity-inventory.h"
NAMED_INVENTORY_SOURCE = BINDER / "identity/named-identity-inventory.cc"
NAMED_INVENTORY_QUERY_SOURCE = Path(
    "products/zomlang/compiler/driver/query/binding/named-identity-inventory-query.cc"
)
NAMED_INVENTORY_QUERY_TEST = Path(
    "products/zomlang/tests/unittests/compiler/driver/named-identity-inventory-query-test.cc"
)
INCREMENTAL_BINDING_QUERY_TEST = Path(
    "products/zomlang/tests/unittests/compiler/driver/incremental-binding-query-adapter-test.cc"
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
REMOVED_SYMBOL_ROOT = Path("products/zomlang/compiler/symbol")
SCHEMA_INCLUDE = '#include "zomlang/compiler/binder/binding-fact-schema.def"'


def source_files() -> dict[Path, str]:
    files: dict[Path, str] = {}
    for relative_root in (Path("products/zomlang/compiler"), Path("products/zomlang/tests")):
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


def type_body(text: str, name: str) -> str:
    match = re.search(rf"\bclass\s+{re.escape(name)}\s+final\s*\{{", text)
    if match is None:
        return ""
    depth = 1
    for index in range(match.end(), len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[match.end() : index]
    return ""


def count_in(files: dict[Path, str], paths: tuple[Path, ...], marker: str) -> int:
    return sum(files.get(path, "").count(marker) for path in paths)


def check_required_files(files: dict[Path, str], errors: list[str]) -> None:
    for path in required_files():
        if path not in files and not (ROOT / path).exists():
            errors.append(f"{path}: required Binder architecture file is missing")
    for path in REMOVED_BINDER_FILES:
        if path in files or (ROOT / path).exists():
            errors.append(f"{path}: removed Binder rail must not exist")
    if (ROOT / REMOVED_SYMBOL_ROOT).exists():
        errors.append(f"{REMOVED_SYMBOL_ROOT}: removed symbol rail must not exist")


def check_removed_rail(files: dict[Path, str], errors: list[str]) -> None:
    forbidden = (
        '#include "zomlang/compiler/binder/binder.h"',
        '#include "zomlang/compiler/symbol/',
        "symbol::SymbolTable",
        "DefinitionIdentityMap",
        "DeclCollector",
        "ImportResolver",
        "NameResolver",
    )
    for path, text in files.items():
        if TESTS in path.parents:
            continue
        for marker in forbidden:
            if marker in text:
                errors.append(f"{path}: removed Binder or Symbol rail remains: {marker}")


def check_private_publication(files: dict[Path, str], errors: list[str]) -> None:
    metadata = files.get(METADATA_HEADER, "")
    for name in ("VerifiedBindingMetadata", "VerifiedExportSurface"):
        body = type_body(metadata, name)
        constructor = f"explicit {name}(zc::Own<Impl>&& impl) noexcept;"
        private = body.find("private:")
        if private < 0 or body.find(constructor, private) < 0:
            errors.append(f"{METADATA_HEADER}: {name} constructor must remain private")
        marker = f"zc::heap<{name}::Impl>"
        count = count_in(files, PRODUCTION_COMPONENTS, marker)
        if count != 1:
            errors.append(f"{PUBLICATION_SOURCE}: {name} must have one publication allocation")
        for path in PRODUCTION_COMPONENTS:
            if path != PUBLICATION_SOURCE and marker in files.get(path, ""):
                errors.append(f"{path}: {name} publication authority escaped")
    publication = files.get(PUBLICATION_SOURCE, "")
    if "BindingVerifier::publishCandidate(" not in publication:
        errors.append(f"{PUBLICATION_SOURCE}: verified publication factory is missing")


def check_pipeline(files: dict[Path, str], errors: list[str]) -> None:
    run = files.get(RUN_SOURCE, "")
    for marker in (
        "BindingBuilder::build(input, diagnostics)",
        "candidate.is<BinderInvariantFact>()",
        "BindingVerifier::verify(input,",
    ):
        if marker not in run:
            errors.append(f"{RUN_SOURCE}: fail-closed Binder pipeline is disconnected: {marker}")
    binder_sources = tuple(path for path in files if BINDER in path.parents and path.suffix == ".cc")
    builder_implementations = sum(
        len(
            re.findall(
                r"\bBindingCandidateResult\s+BindingBuilder::build\(", files.get(path, "")
            )
        )
        for path in binder_sources
    )
    if builder_implementations != 1:
        errors.append(f"{BUILDER_SOURCE}: BindingBuilder::build must have one implementation")
    verifier_implementations = sum(
        len(
            re.findall(
                r"\bBindingVerificationResult\s+BindingVerifier::verify\(",
                files.get(path, ""),
            )
        )
        for path in binder_sources
    )
    if verifier_implementations != 1:
        errors.append(f"{VERIFIER_SOURCE}: BindingVerifier::verify must have one implementation")
    if count_in(files, binder_sources, "BindingDifferentialOracle::verify(") != 0:
        errors.append("production Binder must not implement the test-only differential oracle")
    if count_in(files, TEST_ORACLES, "BindingDifferentialOracle::verify(") != 1:
        errors.append(f"{TESTS}: differential oracle must have one test-only implementation")


def check_verifier_independence(files: dict[Path, str], errors: list[str]) -> None:
    for path in VERIFICATION_COMPONENTS:
        text = files.get(path, "")
        for header in PRODUCER_HEADERS:
            if header in text:
                errors.append(f"{path}: production verifier includes producer header {header}")
        for symbol in PRODUCER_SYMBOLS:
            if symbol in text:
                errors.append(f"{path}: production verifier reuses producer algorithm {symbol}")
    for path in SEMANTIC_TEST_ORACLES:
        text = files.get(path, "")
        for header in PRODUCER_HEADERS:
            if header in text:
                errors.append(f"{path}: semantic mutation oracle includes producer header {header}")
        for symbol in PRODUCER_SYMBOLS:
            if symbol in text:
                errors.append(f"{path}: semantic mutation oracle reuses producer algorithm {symbol}")
    stable_header = files.get(STABLE_HEADER_VERIFIER_SOURCE, "")
    for marker in (
        "DefinitionHeaderProducer::",
        "ImplementationHeaderProducer::",
        '#include "zomlang/compiler/binder/stable/definition/header-producer.h"',
        '#include "zomlang/compiler/binder/stable/implementation/header-producer.h"',
    ):
        if marker in stable_header:
            errors.append(
                f"{STABLE_HEADER_VERIFIER_SOURCE}: stable header verifier reuses producer: {marker}"
            )
    for marker in (
        "definitionEntry(context, queryKey.definition())",
        "definitionAuthority(context, queryKey.definition())",
        "implementationEntry(context, queryKey.occurrence().implementation())",
        "implementationOccurrence(context, queryKey.occurrence())",
        "CandidateVerifier::reconstruct(",
        "completeAuthority(context,",
        "matchesOwners(context,",
        "canonicalRoundTrip(candidate)",
    ):
        if marker not in stable_header:
            errors.append(
                f"{STABLE_HEADER_VERIFIER_SOURCE}: independent header verification is incomplete: "
                f"{marker}"
            )
    stable_header_test = files.get(STABLE_HEADER_VERIFIER_TEST, "")
    for marker in (
        "verifies every equal implementation occurrence independently",
        "rejects a candidate from another complete source context",
    ):
        if marker not in stable_header_test:
            errors.append(
                f"{STABLE_HEADER_VERIFIER_TEST}: stable header regression is missing: {marker}"
            )
    verifier = files.get(VERIFIER_SOURCE, "")
    for marker in (
        "bindingCandidateHasForeignContext(input, candidate)",
        "bindingCandidateHasInvalidSourceRange(input, candidate)",
        "verifyBindingCandidateStructure(input, candidate)",
        "verifyBindingCaptureSemantics(input, candidate)",
        "verifyBindingContextSemantics(input, candidate)",
        "verifyBindingControlSemantics(input, candidate)",
        "publishCandidate(zc::mv(candidate))",
    ):
        if marker not in verifier:
            errors.append(f"{VERIFIER_SOURCE}: verifier stage is disconnected: {marker}")
    structure_stage = verifier.find("verifyBindingCandidateStructure(input, candidate)")
    capture_stage = verifier.find("verifyBindingCaptureSemantics(input, candidate)")
    context_stage = verifier.find("verifyBindingContextSemantics(input, candidate)")
    control_stage = verifier.find("verifyBindingControlSemantics(input, candidate)")
    source_rejection = verifier.find("if (!candidate.sourceFailures.empty())")
    publication = verifier.find("publishCandidate(zc::mv(candidate))")
    if not 0 <= structure_stage < capture_stage < context_stage < control_stage < source_rejection < publication:
        errors.append(
            f"{VERIFIER_SOURCE}: capture, context, and control semantics must run after structure and "
            "before source rejection and publication"
        )
    capture_validator = files.get(CAPTURE_VALIDATOR_SOURCE, "")
    for marker in (
        "class CaptureSemanticValidator final",
        "buildScopeIndex()",
        "buildClosures()",
        "verifyClosureDomains()",
        "verifyExplicitCaptures()",
        "processReference(",
        "verifyInferredCaptures()",
        "verifyBindingCaptureSemantics(",
    ):
        if marker not in capture_validator:
            errors.append(
                f"{CAPTURE_VALIDATOR_SOURCE}: independent capture domain is incomplete: {marker}"
            )
    context_validator = files.get(CONTEXT_VALIDATOR_SOURCE, "")
    for marker in (
        "class BindingContextSemanticValidator final",
        "buildAstIndex()",
        "buildCandidateIndex()",
        "buildReceiverIndex()",
        "contextualSelfOwner(",
        "verifySelfTypes()",
        "activeReceiver(",
        "receiverIsAccessible(",
        "verifyThisBindings()",
        "verifyBindingContextSemantics(",
    ):
        if marker not in context_validator:
            errors.append(
                f"{CONTEXT_VALIDATOR_SOURCE}: independent context domain is incomplete: {marker}"
            )
    for marker in ("verifySelfTypes()", "verifyThisBindings()"):
        if context_validator.count(marker) != 2:
            errors.append(
                f"{CONTEXT_VALIDATOR_SOURCE}: context verification entrypoint is disconnected: "
                f"{marker}"
            )
    control_validator = files.get(CONTROL_VALIDATOR_SOURCE, "")
    for marker in (
        "class ControlSemanticValidator final",
        "reconstructLabels()",
        "visitControl(tree.root())",
        "verifyExplicitControl(node, isBreak, ast::IdentId(labelWord));",
        "verifyImplicitControl(node, isBreak);",
        "verifySuccessfulControl(",
        "verifyFailedControl(",
        "isControlDomainFailure(",
        "if (!labelsAreCanonicallyOrdered())",
        "node.value <= previousControlNode",
        "verifyBindingControlSemantics(",
    ):
        if marker not in control_validator:
            errors.append(
                f"{CONTROL_VALIDATOR_SOURCE}: independent control domain is incomplete: {marker}"
            )
    tests = files.get(TEST_SOURCE, "")
    for marker in (
        "BindingVerifier.RejectsMalformedContextualSelfFacts",
        "BindingVerifier.RejectsWrongThisExpressionReceiverTarget",
        "BindingVerifier.RejectsMalformedContextFailureFacts",
    ):
        if marker not in tests:
            errors.append(f"{TEST_SOURCE}: production context mutation is missing: {marker}")
    for removed in (
        "BindingDifferentialOracle.RejectsMalformedContextualSelfFacts",
        "BindingDifferentialOracle.RejectsWrongThisExpressionReceiverTarget",
    ):
        if removed in tests:
            errors.append(f"{TEST_SOURCE}: context mutation still depends on test oracle: {removed}")
    for marker in (
        "BindingVerifier.RejectsMissingAdditionalReorderedAndMutatedLabels",
        "BindingVerifier.RejectsMalformedLabelDuplicateFailuresAndDeclarationBindings",
        "BindingVerifier.RejectsMalformedExplicitLabelSuccessPairs",
        "BindingVerifier.RejectsMalformedExplicitLabelFailures",
        "BindingVerifier.RejectsMissingAdditionalAndReorderedControlTransfers",
        "BindingVerifier.RejectsInvalidControlTargetsAndSources",
        "BindingVerifier.EnforcesControlFailureXor",
    ):
        if marker not in tests:
            errors.append(f"{TEST_SOURCE}: production control mutation is missing: {marker}")
    for removed in (
        "BindingDifferentialOracle.RejectsMissingAdditionalReorderedAndMutatedLabels",
        "BindingDifferentialOracle.RejectsMalformedLabelDuplicateFailuresAndDeclarationBindings",
        "BindingDifferentialOracle.RejectsMalformedExplicitLabelSuccessPairs",
        "BindingDifferentialOracle.RejectsMalformedExplicitLabelFailures",
        "BindingDifferentialOracle.RejectsMissingAdditionalAndReorderedControlTransfers",
        "BindingDifferentialOracle.RejectsInvalidControlTargetsAndSources",
        "BindingDifferentialOracle.EnforcesControlFailureXor",
    ):
        if removed in tests:
            errors.append(f"{TEST_SOURCE}: control mutation still depends on test oracle: {removed}")
    validator = files.get(VALIDATOR_SOURCE, "")
    for marker in (
        "hasCompleteLexicalBindingSites(",
        "verifyCandidateStructure(",
        "expectedScopeKindForSyntax(",
        "bindingCandidateHasForeignContext(",
        "bindingCandidateHasInvalidSourceRange(",
    ):
        if marker not in validator:
            errors.append(f"{VALIDATOR_SOURCE}: structural domain is incomplete: {marker}")

    stable = files.get(STABLE_IDENTITY_VERIFIER_SOURCE, "")
    for marker in (
        "DefinitionInventory::collect(",
        "CanonicalHeaderTypeProducer::",
        "CanonicalDefinitionHeaderProducer::",
        "CanonicalImplHeaderProducer::",
        "CandidateProducer::produce(",
    ):
        if marker in stable:
            errors.append(
                f"{STABLE_IDENTITY_VERIFIER_SOURCE}: stable verifier reuses producer discovery: {marker}"
            )
    for marker in (
        "class StableSyntaxOracle final",
        "collectSyntaxPaths(ast::NodeId node",
        "stableOwnerChain = savedStableOwnerChain && identity::isStableDefinitionKind(kind);",
        "parents.add(StructuralIdentityParent{StructuralIdentityParentKind::Impl, node});",
        "const CanonicalHeaderSyntaxView headerSyntax(syntax.definitions(), syntax.implementations());",
        "StableIdentityPreAdmission.IndependentlyReconstructsNestedStableOwnerChains",
        "StableIdentityPreAdmission.ExcludesDefinitionsBelowAnonymousOwners",
        "findFirstDuplicateGenericParameter(",
        "if (prior.name != nameValue) { continue; }",
        "stableDefinitionGenericBinder(tree, entry.node)",
        "stableImplementationGenericBinder(tree, entry.node)",
        "StableIdentityPreAdmission.RejectsTheFirstDuplicateGenericParameter",
        "StableIdentityPreAdmission.RejectsNfcEquivalentGenericParameters",
    ):
        source = files.get(TEST_SOURCE, "") if marker.startswith("StableIdentity") else stable
        if marker not in source:
            errors.append(
                f"{STABLE_IDENTITY_VERIFIER_SOURCE}: independent stable verifier marker is missing: {marker}"
            )
    classification_markers = (
        ("ExternDecl", "Function"),
        ("ExternVarDecl", "Static"),
        ("UnitVariant", "EnumVariant"),
        ("TupleVariant", "EnumVariant"),
        ("EnumDeclaration", "Enum"),
        ("FunctionDecl", "Function"),
        ("ClassDecl", "Class"),
        ("StructDecl", "Struct"),
        ("InterfaceDecl", "Interface"),
        ("ErrorDecl", "Error"),
        ("AliasDecl", "TypeAlias"),
        ("MethodDecl", "Method"),
        ("FieldDecl", "Field"),
        ("AssociatedTypeDecl", "AssociatedType"),
        ("ConstructorDecl", "Constructor"),
        ("DestructorDecl", "Destructor"),
        ("ClassConstDecl", "Constant"),
    )
    for syntax_kind, definition_kind in classification_markers:
        pattern = (
            rf"case ast::SyntaxKind::{syntax_kind}:\s*"
            rf"(?:(?!case ast::SyntaxKind::).)*?"
            rf"visitStableDefinition\(.*?identity::DefinitionKind::{definition_kind},"
        )
        if re.search(pattern, stable, re.DOTALL) is None:
            errors.append(
                f"{STABLE_IDENTITY_VERIFIER_SOURCE}: missing stable mapping {syntax_kind} -> {definition_kind}"
            )

    canonical_header = files.get(CANONICAL_HEADER_VERIFIER_HEADER, "") + files.get(
        CANONICAL_HEADER_VERIFIER_SOURCE, ""
    )
    if "const DefinitionInventory&" in canonical_header:
        errors.append(
            f"{CANONICAL_HEADER_VERIFIER_SOURCE}: canonical verifier must consume an explicit syntax view"
        )
    for marker in (
        "class CanonicalHeaderSyntaxView final",
        "const CanonicalHeaderSyntaxView& syntax",
    ):
        if marker not in canonical_header:
            errors.append(
                f"{CANONICAL_HEADER_VERIFIER_SOURCE}: independent syntax view marker is missing: {marker}"
            )

    driver = files.get(DRIVER_SESSION_SOURCE, "")
    driver_test = files.get(DRIVER_SESSION_TEST, "")
    for marker in (
        "StableIdentityCandidateSourceFailureKind::DuplicateGenericParameter",
        "BinderDiagnosticCode::DuplicateIdentifier",
        "BindingDiagnosticAdapter::emitRedeclaration(",
    ):
        if marker not in driver:
            errors.append(
                f"{DRIVER_SESSION_SOURCE}: duplicate generic pre-admission is disconnected: {marker}"
            )
    for marker in (
        "CompilerSession rejects duplicate generic binders before registry mutation",
        "registries.genericParameters().size() == 0",
        "registries.callableParameters().size() == 0",
    ):
        if marker not in driver_test:
            errors.append(
                f"{DRIVER_SESSION_TEST}: duplicate generic pre-admission regression is incomplete: {marker}"
            )


def check_schema_wiring(files: dict[Path, str], errors: list[str]) -> None:
    users = {
        VERIFIER_HEADER: "candidate fact record",
        METADATA_HEADER: "published fact accessors",
        CODEC_SOURCE: "canonical fact sequence codec",
        PUBLICATION_SOURCE: "published accessor implementation",
        TESTS / "binding-differential-oracle.cc": "differential fact census",
    }
    for path, role in users.items():
        if SCHEMA_INCLUDE not in files.get(path, ""):
            errors.append(f"{path}: {role} does not consume binding-fact-schema.def")
    codec = files.get(CODEC_SOURCE, "")
    for marker in ("encodeFactRecord(", "encodeFactSequence(", "stableTag"):
        if marker not in codec:
            errors.append(f"{CODEC_SOURCE}: schema-driven canonical codec is incomplete: {marker}")
    test_cmake = files.get(TEST_CMAKE, "")
    if "binder-fact-schema" not in test_cmake or "check-binder-fact-schema.py --check" not in test_cmake:
        errors.append(f"{TEST_CMAKE}: Binder fact schema gate is not wired")


def normalized_cpp(text: str) -> str:
    return re.sub(r"\s+", "", text)


def capability_rows(schema: str) -> list[tuple[str, str, tuple[str, ...]]]:
    rows: list[tuple[str, str, tuple[str, ...]]] = []
    pattern = re.compile(
        r"ZOM_STABLE_BINDING_CAPABILITY_QUERY\((.*?)\)\s*(?=ZOM_STABLE_BINDING_)",
        re.DOTALL,
    )
    for match in pattern.finditer(schema):
        arguments = [part.strip() for part in match.group(1).split(",")]
        if len(arguments) != 14:
            continue
        failures = tuple(
            alternative.strip() for alternative in arguments[7].split("|")
        )
        rows.append((arguments[0], arguments[4], failures))
    return rows


def descriptor_body(text: str, name: str) -> str:
    match = re.search(
        rf"\b(?:class|struct)\s+{re.escape(name)}\s+final\s*\{{", text
    )
    if match is None:
        return ""
    depth = 1
    for index in range(match.end(), len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[match.end():index]
    return ""


def qualified_failure(alternative: str) -> str:
    if alternative.startswith("SourceRejection<"):
        payload = alternative.removeprefix("SourceRejection<").removesuffix(">")
        if payload == "DiagnosticFact":
            payload = "diagnostics::DiagnosticFact"
        return f"query::SourceRejection<{payload}>"
    if alternative.startswith("KeyRejection<"):
        payload = alternative.removeprefix("KeyRejection<").removesuffix(">")
        if payload == "BinderKeyFailure":
            payload = "binder::BinderKeyFailure"
        return f"query::KeyRejection<{payload}>"
    return alternative


def check_stable_binding_wiring(files: dict[Path, str], errors: list[str]) -> None:
    binder_cmake = files.get(BINDER_CMAKE, "")
    test_cmake = files.get(TEST_CMAKE, "")
    for source in (STABLE_FACTS_SOURCE, STABLE_CODEC_SOURCE):
        marker = f"${{CMAKE_CURRENT_SOURCE_DIR}}/{binder_source(source)}"
        if binder_cmake.count(marker) != 1:
            errors.append(
                f"{BINDER_CMAKE}: stable source must appear exactly once: {binder_source(source)}"
            )
    required_registrations = (
        'add_ztest_unit_test("stable-binding-facts-test"',
        "add_test(NAME stable-binding-schema\n",
        "add_test(NAME stable-binding-schema-negative\n",
        "add_test(NAME stable-binding-landing-scope-negative\n",
    )
    for marker in required_registrations:
        if test_cmake.count(marker) != 1:
            errors.append(f"{TEST_CMAKE}: stable Binder registration drift: {marker.strip()}")
    if "check-stable-binding-schema.py --check" not in test_cmake:
        errors.append(f"{TEST_CMAKE}: stable schema check is not wired")
    if "check-stable-binding-schema.py --self-test" not in test_cmake:
        errors.append(f"{TEST_CMAKE}: stable schema negative check is not wired")
    if "check-landing-scope.py --self-test" not in test_cmake:
        errors.append(f"{TEST_CMAKE}: stable landing-scope negative check is not wired")

    production = {
        path: text
        for path, text in files.items()
        if Path("products/zomlang/compiler") in path.parents and path != STABLE_SCHEMA
    }
    combined = "\n".join(production.values())
    normalized_combined = normalized_cpp(combined)
    for name, capability, failures in capability_rows(files.get(STABLE_SCHEMA, "")):
        owners = [
            (path, descriptor_body(text, name))
            for path, text in production.items()
            if descriptor_body(text, name)
        ]
        proof_prefix = f"static_assert(zc::isSameType<typename{name}::"
        if not owners:
            if proof_prefix in normalized_combined:
                errors.append(
                    f"{STABLE_SCHEMA}: future capability row {name} must remain inert"
                )
            continue
        if len(owners) != 1:
            errors.append(f"{STABLE_SCHEMA}: capability descriptor {name} must have one owner")
            continue
        owner_path, body = owners[0]
        normalized_body = normalized_cpp(body)
        expected_failures = ",".join(qualified_failure(value) for value in failures)
        expected_failure_alias = (
            "usingFailureAlternatives="
            f"query::CapabilityFailureList<{expected_failures}>;"
        )
        if f"usingCapability={capability};" not in normalized_body:
            errors.append(
                f"{owner_path}: {name}::Capability disagrees with the owned schema row"
            )
        if expected_failure_alias not in normalized_body:
            errors.append(
                f"{owner_path}: {name}::FailureAlternatives disagrees with the owned schema row"
            )
        capability_proof = (
            f"static_assert(zc::isSameType<typename{name}::Capability,{capability}>());"
        )
        failure_proof = (
            "static_assert(zc::isSameType<typename"
            f"{name}::FailureAlternatives,query::CapabilityFailureList<{expected_failures}>>());"
        )
        if capability_proof not in normalized_combined:
            errors.append(f"{owner_path}: {name} capability equality proof is missing")
        if failure_proof not in normalized_combined:
            errors.append(
                f"{owner_path}: {name} failure-alternatives equality proof is missing"
            )


def check_cmake_boundaries(files: dict[Path, str], errors: list[str]) -> None:
    binder_cmake = files.get(BINDER_CMAKE, "")
    test_cmake = files.get(TEST_CMAKE, "")
    for path in PRODUCTION_COMPONENTS:
        if binder_source(path) not in binder_cmake:
            errors.append(f"{BINDER_CMAKE}: production component omitted: {binder_source(path)}")
    for path in TEST_ORACLES:
        if path.name not in test_cmake:
            errors.append(f"{TEST_CMAKE}: test-only oracle omitted: {path.name}")
        if path.name in binder_cmake:
            errors.append(f"{BINDER_CMAKE}: test-only oracle leaked into production: {path.name}")


def check_layering(files: dict[Path, str], errors: list[str]) -> None:
    for path, text in files.items():
        if BINDER not in path.parents or TESTS in path.parents:
            continue
        for forbidden in (
            '"zomlang/compiler/checker/',
            '"zomlang/compiler/ir/',
            '"zomlang/compiler/symbol/',
        ):
            if forbidden in text:
                errors.append(f"{path}: Binder layer depends on downstream layer {forbidden}")
        if path.name != "binding-diagnostic-adapter.cc" and "engine.diagnose<" in text:
            errors.append(f"{path}: raw diagnostics bypass the typed Binder adapter")


def check_module_owned_capture_boundaries(files: dict[Path, str], errors: list[str]) -> None:
    body_binding = files.get(BODY_BINDING_SOURCE, "")
    capture_access_match = re.search(
        r"CaptureAccess captureAccess\(.*?\n  }\n\n  void publishLocalFact",
        body_binding,
        re.DOTALL,
    )
    if capture_access_match is None:
        errors.append(f"{BODY_BINDING_SOURCE}: capture access boundary is missing")
    else:
        capture_access = capture_access_match.group(0)
        declaring_scope_check = capture_access.find(
            "if (scopeIndex == targetDeclaringScope) { return CaptureAccess::Allowed; }"
        )
        named_function_boundary = capture_access.find(
            "if (scope.kind == ScopeKind::Function) { return CaptureAccess::Denied; }"
        )
        module_boundary = capture_access.find(
            "if (scope.kind == ScopeKind::Module || scope.parent == zc::none)"
        )
        if (
            declaring_scope_check < 0
            or named_function_boundary < 0
            or module_boundary < 0
            or not declaring_scope_check < named_function_boundary < module_boundary
        ):
            errors.append(
                f"{BODY_BINDING_SOURCE}: declaring scope must authorize module-owned capture "
                "before callable and module traversal boundaries"
            )
    for removed in ("CaptureAccessPurpose", "owningCallableScopeIndices"):
        if removed in body_binding:
            errors.append(f"{BODY_BINDING_SOURCE}: obsolete capture boundary remains: {removed}")

    closure_free = files.get(CLOSURE_FREE_VARIABLES_SOURCE, "")
    for marker in (
        "uint32_t captureBoundaryScope = kMissingIndex;",
        "if (scope.kind == ScopeKind::Module) {\n          binding.captureBoundaryScope = scopeIndex;",
        "if (scopeIndex == binding.captureBoundaryScope)",
    ):
        if marker not in closure_free:
            errors.append(
                f"{CLOSURE_FREE_VARIABLES_SOURCE}: module-owned free-variable boundary is "
                f"incomplete: {marker}"
            )
    for removed in ("owningCallableScope", "owningCallableScopeIndices"):
        if removed in closure_free:
            errors.append(
                f"{CLOSURE_FREE_VARIABLES_SOURCE}: obsolete callable-only boundary remains: "
                f"{removed}"
            )

    tests = files.get(TEST_SOURCE, "")
    for marker in (
        "ClosureFreeVariables.CapturesModuleOwnedPatternAndLocalReferences",
        "ExplicitClosureCaptures.CapturesModuleOwnedPatternAndLocalItems",
        "BindingVerifier.RejectsSemanticallyMalformedClosureCaptureFacts",
    ):
        if marker not in tests:
            errors.append(f"{TEST_SOURCE}: module-owned capture regression is missing: {marker}")
    for removed in (
        "ClosureFreeVariables.RejectsModuleOwnedPatternAndLocalReferences",
        "ExplicitClosureCaptures.RejectsModuleOwnedPatternAndLocalItems",
    ):
        if removed in tests:
            errors.append(f"{TEST_SOURCE}: obsolete module-owned rejection remains: {removed}")


def check_module_body_syntax(files: dict[Path, str], errors: list[str]) -> None:
    header = files.get(MODULE_BODY_HEADER, "")
    value = files.get(MODULE_BODY_VALUE_SOURCE, "")
    producer = files.get(MODULE_BODY_PRODUCER_SOURCE, "")
    verifier = files.get(MODULE_BODY_VERIFIER_SOURCE, "")
    tests = files.get(MODULE_BODY_TEST_SOURCE, "")
    binder_cmake = files.get(BINDER_CMAKE, "")
    test_cmake = files.get(TEST_CMAKE, "")

    for marker in (
        "class DetachedModuleBodyNode final",
        "class ModuleBodySyntax final",
        "class ModuleBodyProvenance final",
        "DefinitionBoundary = 0x02",
        "LocalSyntaxPath path;",
        "ast::NodeId node;",
    ):
        if marker not in header:
            errors.append(f"{MODULE_BODY_HEADER}: module-body value contract is incomplete: {marker}")
    for marker in (
        'kModuleBodySyntaxDomain = "zom.module-body-syntax"',
        'kModuleBodyProvenanceDomain = "zom.module-body-provenance"',
        "kAstSchemaFingerprint",
        "validateCanonicalFields(",
        "validPreorder(",
        "LocalSyntaxPath::decodeCanonical(",
    ):
        if marker not in value:
            errors.append(f"{MODULE_BODY_VALUE_SOURCE}: strict module-body codec is incomplete: {marker}")
    for marker in (
        "DefinitionInventory::collect(tree)",
        "DetachedModuleBodyNode::definitionBoundary(",
        "provenance.add(ModuleBodyProvenanceEntry",
        "valid = visit(child, path);",
    ):
        if marker not in producer:
            errors.append(f"{MODULE_BODY_PRODUCER_SOURCE}: module-body producer is incomplete: {marker}")
    for forbidden in ("ModuleBodySyntaxProducer::", "DefinitionInventory::collect("):
        if forbidden in verifier:
            errors.append(
                f"{MODULE_BODY_VERIFIER_SOURCE}: independent verifier reuses producer authority: {forbidden}"
            )
    for marker in (
        "independentlySelectItems(",
        "for (const auto item : values) { collectIndependentBoundaries(tree, item, census); }",
        "independentlyEncodeFields(",
        "class IndependentWalker final",
        "ModuleBodySyntaxVerifier::reconstruct(",
        "reconstructed.syntax == projection.syntax",
        "reconstructed.provenance == projection.provenance",
    ):
        if marker not in verifier:
            errors.append(f"{MODULE_BODY_VERIFIER_SOURCE}: independent verifier is incomplete: {marker}")
    for path in (MODULE_BODY_VALUE_SOURCE, MODULE_BODY_PRODUCER_SOURCE, MODULE_BODY_VERIFIER_SOURCE):
        cmake_entry = f"${{CMAKE_CURRENT_SOURCE_DIR}}/{binder_source(path)}"
        if cmake_entry not in binder_cmake:
            errors.append(
                f"{BINDER_CMAKE}: module-body production component omitted: {binder_source(path)}"
            )
    if MODULE_BODY_TEST_SOURCE.name not in test_cmake:
        errors.append(f"{TEST_CMAKE}: module-body native test is omitted")
    for marker in (
        "Module body syntax canonicalizes implicit, declared, and inline roots equally",
        "Module body syntax retains implementation headers and prunes definitions",
        "Module body syntax backdates across range-only source edits",
        "Module body codecs reject trailing data and preserve exact values",
        "Module body producer and verifier reject incomplete boundary inventories",
    ):
        if marker not in tests:
            errors.append(f"{MODULE_BODY_TEST_SOURCE}: module-body regression is missing: {marker}")


def check_named_definition_inventory(files: dict[Path, str], errors: list[str]) -> None:
    header = files.get(NAMED_INVENTORY_HEADER, "")
    inventory = files.get(NAMED_INVENTORY_SOURCE, "")
    query = files.get(NAMED_INVENTORY_QUERY_SOURCE, "")
    query_tests = files.get(NAMED_INVENTORY_QUERY_TEST, "")
    incremental_tests = files.get(INCREMENTAL_BINDING_QUERY_TEST, "")

    entry_body = type_body(header, "NamedDefinitionInventoryEntry")
    for marker in (
        "identity::DefinitionIdentityRecord recordField;",
        "DefinitionBodyDisposition bodyDispositionField;",
        "const identity::DefinitionIdentityRecord& record() const noexcept;",
        "DefinitionBodyDisposition bodyDisposition() const noexcept;",
    ):
        if marker not in entry_body:
            errors.append(
                f"{NAMED_INVENTORY_HEADER}: typed definition inventory entry is incomplete: {marker}"
            )
    if "canonicalRecord" in entry_body:
        errors.append(
            f"{NAMED_INVENTORY_HEADER}: byte-only definition record accessor remains"
        )
    for marker in (
        "identity::DefinitionIdentityRecord::decodeCanonical(",
        "identity::DefinitionKey::compute(recordValue) != keyValue",
        "isStableBindingValue(bodyDisposition)",
        "encoder.encodeUint8(static_cast<uint8_t>(entry.bodyDisposition()))",
    ):
        if marker not in inventory:
            errors.append(
                f"{NAMED_INVENTORY_SOURCE}: definition inventory codec is incomplete: {marker}"
            )

    if "admittedDefinitionInventory" in query:
        errors.append(
            f"{NAMED_INVENTORY_QUERY_SOURCE}: authority-only definition inventory path remains"
        )
    for marker in (
        "providerDefinitionInventory(",
        "verifierDefinitionInventory(",
        "verifiedIdentityCandidates(",
    ):
        if marker not in query:
            errors.append(
                f"{NAMED_INVENTORY_QUERY_SOURCE}: selected-syntax inventory path is missing: {marker}"
            )
    for marker in (
        "case ast::SyntaxKind::FunctionDecl:",
        "case ast::SyntaxKind::ConstructorDecl:",
        "case ast::SyntaxKind::DestructorDecl:",
        "case ast::SyntaxKind::MethodDecl:",
        "case ast::SyntaxKind::FieldDecl:",
        "case ast::SyntaxKind::ClassConstDecl:",
        "ast::kFunctionDeclBodyWord",
        "ast::kConstructorDeclBodyWord",
        "ast::kDestructorDeclBodyWord",
        "ast::kMethodDeclBodyWord",
        "ast::kFieldDeclInitWord",
        "ast::kClassConstDeclInitWord",
        "ast::isLiteralExprKind(kind)",
        "ast::isExprKind(kind)",
        "kind != ast::SyntaxKind::UnsafeBlockExpr",
    ):
        if query.count(marker) != 2:
            errors.append(
                f"{NAMED_INVENTORY_QUERY_SOURCE}: provider/verifier body classification "
                f"must contain two independent occurrences: {marker}"
            )

    provide_start = query.find("NamedDefinitionInventoryQuery::provide(")
    provide_end = query.find("bool NamedDefinitionInventoryQuery::verify(", provide_start)
    verify_start = provide_end
    verify_end = query.find("NamedImplementationInventoryQuery::encodeKey(", verify_start)
    provide_body = query[provide_start:provide_end]
    verify_body = query[verify_start:verify_end]
    for label, body, builder in (
        ("provider", provide_body, "providerDefinitionInventory("),
        ("verifier", verify_body, "verifierDefinitionInventory("),
    ):
        selected_parse = body.find("loadIdentitySource(context, key)")
        admission = body.find("getCapability<StableIdentityAdmissionQuery>(key)")
        if selected_parse < 0 or admission < 0 or selected_parse >= admission:
            errors.append(
                f"{NAMED_INVENTORY_QUERY_SOURCE}: definition inventory {label} "
                "must read selected source and parse before admission"
            )
        if builder not in body:
            errors.append(
                f"{NAMED_INVENTORY_QUERY_SOURCE}: definition inventory {label} "
                f"does not use its independent candidate builder: {builder}"
            )
    if "verifierDefinitionInventory(" in provide_body:
        errors.append(
            f"{NAMED_INVENTORY_QUERY_SOURCE}: provider reuses verifier inventory builder"
        )
    if "providerDefinitionInventory(" in verify_body:
        errors.append(
            f"{NAMED_INVENTORY_QUERY_SOURCE}: verifier reuses provider inventory builder"
        )

    for marker in (
        "DefinitionInventoryRetainsCanonicalRecordsAndBodies",
        "DefinitionInventoryRejectsInvalidWireRelations",
    ):
        if marker not in query_tests:
            errors.append(
                f"{NAMED_INVENTORY_QUERY_TEST}: definition inventory mutation coverage "
                f"is missing: {marker}"
            )
    for marker in (
        "Named definition inventory derives executable bodies from selected syntax",
        "definitionReadOrdinal == 6",
        "definitionSelectedReads == 2",
        "definitionParseReads == 2",
        "definitionAdmissionReads == 2",
    ):
        if marker not in incremental_tests:
            errors.append(
                f"{INCREMENTAL_BINDING_QUERY_TEST}: selected-syntax definition inventory "
                f"coverage is missing: {marker}"
            )


def check_size_boundaries(files: dict[Path, str], errors: list[str]) -> None:
    limits = {
        VERIFIER_SOURCE: 300,
        BUILDER_SOURCE: 1000,
        CODEC_SOURCE: 1000,
        VALIDATOR_SOURCE: 1800,
        CAPTURE_VALIDATOR_SOURCE: 900,
        CONTROL_VALIDATOR_SOURCE: 900,
        CONTEXT_VALIDATOR_SOURCE: 700,
        PUBLICATION_SOURCE: 400,
    }
    for path, limit in limits.items():
        count = len(files.get(path, "").splitlines())
        if count > limit:
            errors.append(f"{path}: {count} lines exceeds domain boundary {limit}")


def check(files: dict[Path, str]) -> list[str]:
    errors: list[str] = []
    check_required_files(files, errors)
    check_removed_rail(files, errors)
    check_private_publication(files, errors)
    check_pipeline(files, errors)
    check_verifier_independence(files, errors)
    check_schema_wiring(files, errors)
    check_stable_binding_wiring(files, errors)
    check_cmake_boundaries(files, errors)
    check_layering(files, errors)
    check_module_owned_capture_boundaries(files, errors)
    check_module_body_syntax(files, errors)
    check_named_definition_inventory(files, errors)
    check_size_boundaries(files, errors)
    return sorted(set(errors))


def self_test(files: dict[Path, str]) -> list[str]:
    baseline = check(files)
    if baseline:
        return [f"self-test baseline rejected: {error}" for error in baseline]
    cases = (
        ("old Binder rail", BINDER / "binder.h", "", "class Binder {};\n"),
        ("builder CMake omission", BINDER_CMAKE, "binding-builder.cc", "missing-builder.cc"),
        (
            "producer include in verifier",
            VERIFIER_SOURCE,
            '#include "zc/core/debug.h"',
            '#include "zomlang/compiler/binder/internal/scope-arena.h"',
        ),
        (
            "producer call in verifier",
            VERIFIER_SOURCE,
            "namespace zomlang::compiler::binder {",
            "namespace zomlang::compiler::binder {\n// BindingBuilder::buildCandidate(input);",
        ),
        (
            "stable verifier reuses producer discovery",
            STABLE_IDENTITY_VERIFIER_SOURCE,
            "class StableSyntaxOracle final",
            "// DefinitionInventory::collect(tree);\nclass StableSyntaxOracle final",
        ),
        (
            "stable verifier drops a definition mapping",
            STABLE_IDENTITY_VERIFIER_SOURCE,
            "case ast::SyntaxKind::ClassDecl:\n        visitStableDefinition",
            "case ast::SyntaxKind::MissingClassDecl:\n        visitStableDefinition",
        ),
        (
            "stable verifier changes a definition classification",
            STABLE_IDENTITY_VERIFIER_SOURCE,
            "visitStableDefinition(node, identity::DefinitionKind::Class,",
            "visitStableDefinition(node, identity::DefinitionKind::Struct,",
        ),
        (
            "stable verifier drops owner-chain validation",
            STABLE_IDENTITY_VERIFIER_SOURCE,
            "stableOwnerChain = savedStableOwnerChain && identity::isStableDefinitionKind(kind);",
            "stableOwnerChain = savedStableOwnerChain;",
        ),
        (
            "stable verifier drops NFC duplicate generic comparison",
            STABLE_IDENTITY_VERIFIER_SOURCE,
            "if (prior.name != nameValue) { continue; }",
            "if (true) { continue; }",
        ),
        (
            "stable verifier disconnects definition generic binders",
            STABLE_IDENTITY_VERIFIER_SOURCE,
            "stableDefinitionGenericBinder(tree, entry.node)",
            "ast::NodeId()",
        ),
        (
            "driver drops duplicate generic pre-admission",
            DRIVER_SESSION_SOURCE,
            "StableIdentityCandidateSourceFailureKind::DuplicateGenericParameter",
            "StableIdentityCandidateSourceFailureKind::MissingDuplicateGenericParameter",
        ),
        (
            "driver duplicate generic registry regression is weakened",
            DRIVER_SESSION_TEST,
            "registries.genericParameters().size() == 0",
            "registries.genericParameters().size() == 1",
        ),
        (
            "missing verification stage",
            VERIFIER_SOURCE,
            "verifyBindingCandidateStructure(input, candidate)",
            "missingStructureCheck(input, candidate)",
        ),
        (
            "missing capture semantic stage",
            VERIFIER_SOURCE,
            "verifyBindingCaptureSemantics(input, candidate)",
            "missingCaptureSemanticCheck(input, candidate)",
        ),
        (
            "missing context semantic stage",
            VERIFIER_SOURCE,
            "verifyBindingContextSemantics(input, candidate)",
            "missingContextSemanticCheck(input, candidate)",
        ),
        (
            "missing control semantic stage",
            VERIFIER_SOURCE,
            "verifyBindingControlSemantics(input, candidate)",
            "missingControlSemanticCheck(input, candidate)",
        ),
        (
            "capture validator reuses producer",
            CAPTURE_VALIDATOR_SOURCE,
            '#include "zc/core/debug.h"',
            '#include "zomlang/compiler/binder/internal/closure-free-variables.h"',
        ),
        (
            "capture validator CMake omission",
            BINDER_CMAKE,
            "binding-capture-validator.cc",
            "missing-capture-validator.cc",
        ),
        (
            "context validator reuses producer",
            CONTEXT_VALIDATOR_SOURCE,
            '#include "zc/core/debug.h"',
            '#include "zomlang/compiler/binder/internal/body-binding.h"',
        ),
        (
            "context validator CMake omission",
            BINDER_CMAKE,
            "binding-context-validator.cc",
            "missing-context-validator.cc",
        ),
        (
            "control validator reuses producer",
            CONTROL_VALIDATOR_SOURCE,
            '#include "zc/core/debug.h"',
            '#include "zomlang/compiler/binder/internal/label-facts.h"',
        ),
        (
            "control validator CMake omission",
            BINDER_CMAKE,
            "binding-control-validator.cc",
            "missing-control-validator.cc",
        ),
        (
            "control validator drops implicit target reconstruction",
            CONTROL_VALIDATOR_SOURCE,
            "verifyImplicitControl(node, isBreak);",
            "verifyExplicitControl(node, isBreak, ast::IdentId());",
        ),
        (
            "control validator drops canonical label ordering",
            CONTROL_VALIDATOR_SOURCE,
            "if (!labelsAreCanonicallyOrdered())",
            "if (false)",
        ),
        (
            "control validator drops control fact ordering",
            CONTROL_VALIDATOR_SOURCE,
            "node.value <= previousControlNode",
            "node.value == previousControlNode",
        ),
        (
            "control mutation returns to differential oracle",
            TEST_SOURCE,
            "BindingVerifier.RejectsInvalidControlTargetsAndSources",
            "BindingDifferentialOracle.RejectsInvalidControlTargetsAndSources",
        ),
        (
            "context validator drops receiver verification",
            CONTEXT_VALIDATOR_SOURCE,
            "if (!verifySelfTypes() || !verifyThisBindings()) { return failure; }",
            "if (!verifySelfTypes()) { return failure; }",
        ),
        (
            "context mutation returns to differential oracle",
            TEST_SOURCE,
            "BindingVerifier.RejectsWrongThisExpressionReceiverTarget",
            "BindingDifferentialOracle.RejectsWrongThisExpressionReceiverTarget",
        ),
        (
            "test oracle in production",
            BINDER_CMAKE,
            "add_library(binder STATIC ${BINDER_SRC})",
            "binding-context-oracle.cc\nadd_library(binder STATIC ${BINDER_SRC})",
        ),
        (
            "producer call in semantic oracle",
            TESTS / "binding-control-oracle.cc",
            "namespace zomlang::compiler::binder {",
            "namespace zomlang::compiler::binder {\n// ScopeArenaBuilder::build(input);",
        ),
        (
            "schema disconnected from codec",
            CODEC_SOURCE,
            SCHEMA_INCLUDE,
            '#include "zomlang/compiler/binder/missing-schema.def"',
        ),
        (
            "stable facts source CMake omission",
            BINDER_CMAKE,
            "stable-binding-facts.cc",
            "missing-stable-binding-facts.cc",
        ),
        (
            "stable ztest registration omission",
            TEST_CMAKE,
            'add_ztest_unit_test("stable-binding-facts-test"',
            'add_ztest_unit_test("missing-stable-binding-facts-test"',
        ),
        (
            "implemented descriptor missing capability equality",
            STABLE_FACTS_HEADER,
            "namespace zomlang::compiler::binder {",
            "namespace zomlang::compiler::binder {\n"
            "struct MaterializeModuleGraph final {\n"
            "  using FailureAlternatives = query::CapabilityFailureList<"
            "query::SourceRejection<diagnostics::DiagnosticFact>, "
            "query::KeyRejection<binder::BinderKeyFailure>>;\n"
            "};\n",
        ),
        (
            "implemented descriptor missing failure equality",
            STABLE_FACTS_HEADER,
            "namespace zomlang::compiler::binder {",
            "namespace zomlang::compiler::binder {\n"
            "struct MaterializeModuleGraph final {\n"
            "  using Capability = MaterializedModuleGraph;\n"
            "};\n",
        ),
        (
            "duplicate publication",
            VERIFIER_SOURCE,
            "namespace zomlang::compiler::binder {",
            "namespace zomlang::compiler::binder {\n// zc::heap<VerifiedBindingMetadata::Impl>",
        ),
        (
            "removed symbol rail",
            VERIFIER_SOURCE,
            "namespace zomlang::compiler::binder {",
            "namespace zomlang::compiler::binder {\n// symbol::SymbolTable",
        ),
        (
            "module-owned capture authorization",
            BODY_BINDING_SOURCE,
            "if (scopeIndex == targetDeclaringScope) { return CaptureAccess::Allowed; }",
            "if (scopeIndex == kMissingIndex) { return CaptureAccess::Allowed; }",
        ),
        (
            "module-owned free-variable boundary",
            CLOSURE_FREE_VARIABLES_SOURCE,
            "if (scope.kind == ScopeKind::Module) {\n          "
            "binding.captureBoundaryScope = scopeIndex;",
            "if (scope.kind == ScopeKind::Module) {\n          "
            "binding.captureBoundaryScope = kMissingIndex;",
        ),
        (
            "module-owned explicit-capture regression",
            TEST_SOURCE,
            "ExplicitClosureCaptures.CapturesModuleOwnedPatternAndLocalItems",
            "ExplicitClosureCaptures.RejectsModuleOwnedPatternAndLocalItems",
        ),
        (
            "module-body verifier reuses producer",
            MODULE_BODY_VERIFIER_SOURCE,
            "namespace zomlang::compiler::binder {",
            "namespace zomlang::compiler::binder {\n// ModuleBodySyntaxProducer::produce();",
        ),
        (
            "module-body verifier reuses definition inventory",
            MODULE_BODY_VERIFIER_SOURCE,
            "namespace zomlang::compiler::binder {",
            "namespace zomlang::compiler::binder {\n// DefinitionInventory::collect(tree);",
        ),
        (
            "module-body verifier drops independent boundary census",
            MODULE_BODY_VERIFIER_SOURCE,
            "for (const auto item : values) { collectIndependentBoundaries(tree, item, census); }",
            "for (const auto item : values) { reuseProducerBoundaries(tree, item, census); }",
        ),
        (
            "module-body producer CMake omission",
            BINDER_CMAKE,
            "module-body-syntax-producer.cc",
            "missing-module-body-syntax-producer.cc",
        ),
        (
            "module-body range-shielding regression",
            MODULE_BODY_TEST_SOURCE,
            "Module body syntax backdates across range-only source edits",
            "Module body syntax changes across range-only source edits",
        ),
        (
            "definition inventory provider reuses verifier builder",
            NAMED_INVENTORY_QUERY_SOURCE,
            "providerDefinitionInventory(loaded.value(), admission.lease().capability())",
            "verifierDefinitionInventory(loaded.value(), admission.lease().capability())",
        ),
        (
            "definition inventory drops field body classification",
            NAMED_INVENTORY_QUERY_SOURCE,
            "ast::kFieldDeclInitWord",
            "ast::kMissingFieldDeclInitWord",
        ),
        (
            "definition inventory drops selected syntax test",
            INCREMENTAL_BINDING_QUERY_TEST,
            "Named definition inventory derives executable bodies from selected syntax",
            "Named definition inventory trusts admission body flags",
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
