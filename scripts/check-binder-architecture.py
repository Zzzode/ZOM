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
SITE_HEADER = BINDER_DIR / "definition-site.h"
SITE_SOURCE = BINDER_DIR / "definition-site.cc"
METADATA_HEADER = BINDER_DIR / "binding-metadata.h"
METADATA_SOURCE = BINDER_DIR / "binding-metadata.cc"
VERIFIER_HEADER = BINDER_DIR / "internal" / "binding-verifier.h"
VERIFIER_SOURCE = BINDER_DIR / "binding-verifier.cc"
SCOPE_HEADER = BINDER_DIR / "internal" / "scope-arena.h"
SCOPE_SOURCE = BINDER_DIR / "scope-arena.cc"
SKELETON_HEADER = BINDER_DIR / "internal" / "binding-skeleton.h"
SKELETON_SOURCE = BINDER_DIR / "binding-skeleton.cc"
DIAGNOSTIC_ADAPTER_HEADER = BINDER_DIR / "binding-diagnostic-adapter.h"
DIAGNOSTIC_ADAPTER_SOURCE = BINDER_DIR / "binding-diagnostic-adapter.cc"
DIAGNOSTIC_DEFINITIONS = Path("products/zomlang/compiler/diagnostics/diagnostics-binder.def")
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
        SITE_HEADER,
        SITE_SOURCE,
        METADATA_HEADER,
        METADATA_SOURCE,
        VERIFIER_HEADER,
        VERIFIER_SOURCE,
        SCOPE_HEADER,
        SCOPE_SOURCE,
        SKELETON_HEADER,
        SKELETON_SOURCE,
        DIAGNOSTIC_ADAPTER_HEADER,
        DIAGNOSTIC_ADAPTER_SOURCE,
        DIAGNOSTIC_DEFINITIONS,
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
        (METADATA_HEADER, "VerifiedBindingMetadata"),
        (METADATA_HEADER, "VerifiedExportSurface"),
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
        "VerifiedBindingMetadata": VERIFIER_SOURCE,
        "VerifiedExportSurface": VERIFIER_SOURCE,
    }
    for path, text in files.items():
        if path.suffix not in {".h", ".cc"} or TEST_DIR in path.parents:
            continue
        for name, publication_source in publication_sources.items():
            if path in {
                HEADER,
                PARSED_HEADER,
                INVENTORY_HEADER,
                METADATA_HEADER,
                VERIFIER_HEADER,
            }:
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


def check_frozen_impl_inventory_contract(files: dict[Path, str], errors: list[str]) -> None:
    header = files.get(INVENTORY_HEADER, "")
    source = files.get(INVENTORY_SOURCE, "")
    for required in (
        "struct FrozenImplEntry final",
        "zc::ArrayPtr<const FrozenImplEntry> impls() const;",
        "zc::Maybe<identity::ImplId> implAt(ast::NodeId node) const;",
    ):
        if required not in header:
            errors.append(f"{INVENTORY_HEADER}: incomplete frozen impl inventory: {required}")
    for required in (
        "expectedImplKey(",
        "registries.impls().size() != inventory.impls().size()",
        "registries.impls().find(keyValue)",
        "frozenImpls.add(FrozenImplEntry(",
    ):
        if required not in source:
            errors.append(f"{INVENTORY_SOURCE}: frozen impl authority is disconnected: {required}")
    if "UnsupportedImplInventory" in header or "UnsupportedImplInventory" in source:
        errors.append(f"{INVENTORY_HEADER}: impl inventory compatibility rejection is forbidden")


def check_special_callable_contract(files: dict[Path, str], errors: list[str]) -> None:
    inventory = files.get(INVENTORY_SOURCE, "")
    skeleton = files.get(SKELETON_SOURCE, "")
    tests = files.get(TEST_SOURCE, "")
    for required in (
        "bool permitsAbsentLexicalBinding(identity::DefinitionKind kind)",
        "return kind == identity::DefinitionKind::Constructor ||\n"
        "         kind == identity::DefinitionKind::Destructor;",
        "bindingName == zc::none && !permitsAbsentLexicalBinding(entry.kind)",
    ):
        if required not in inventory:
            errors.append(f"{INVENTORY_SOURCE}: special callable identity contract is disconnected: {required}")
    for required in (
        "return SkeletonEligibility::SpecialCallable;",
        "ast::kConstructorDeclParamsIdWord",
        "ast::kDestructorDeclParamsIdWord",
        "bool hasLexicalBinding(SkeletonEligibility classification)",
        "return classification != SkeletonEligibility::SpecialCallable &&\n"
        "         classification != SkeletonEligibility::Closure;",
        "const bool lexicalBinding = hasLexicalBinding(classification);",
        "classification == SkeletonEligibility::SpecialCallable && !specialCallableScope",
        "if (lexicalBinding) {",
    ):
        if required not in skeleton:
            errors.append(f"{SKELETON_SOURCE}: special callable binding contract is disconnected: {required}")
    if "BindingActivation.PublishesSpecialCallableParameterLists" not in tests:
        errors.append(f"{TEST_SOURCE}: missing special callable activation evidence")


def check_closure_activation_contract(files: dict[Path, str], errors: list[str]) -> None:
    skeleton = files.get(SKELETON_SOURCE, "")
    tests = files.get(TEST_SOURCE, "")
    for required in (
        "case DefinitionKind::Closure:\n      return SkeletonEligibility::Closure;",
        "ast::kFunctionExpressionParamsIdWord",
        "ast::kLambdaExpressionParamsIdWord",
        "case DefinitionKind::Closure:\n      return true;",
        "record.kind != ScopeKind::Function &&\n           record.kind != ScopeKind::Closure",
        "classification != SkeletonEligibility::Closure &&\n"
        "           classification != SkeletonEligibility::Pattern && !skeletonScope",
        "classification == SkeletonEligibility::Closure ||",
        "classification == SkeletonEligibility::Closure\n"
        "              ? DefinitionActivation::ExpressionIntroduction",
    ):
        if required not in skeleton:
            errors.append(f"{SKELETON_SOURCE}: closure activation contract is disconnected: {required}")
    if "BindingActivation.PublishesClosureIdentityAndParameters" not in tests:
        errors.append(f"{TEST_SOURCE}: missing closure activation evidence")


def check_pattern_activation_contract(files: dict[Path, str], errors: list[str]) -> None:
    skeleton = files.get(SKELETON_SOURCE, "")
    tests = files.get(TEST_SOURCE, "")
    for required in (
        "case DefinitionKind::PatternBinding:\n      return SkeletonEligibility::Pattern;",
        "zc::Maybe<DefinitionActivation> patternActivation(",
        "site.value().is<PatternBindingSite>()",
        "case ast::SyntaxKind::ForInStatement:\n      return DefinitionActivation::LoopPattern;",
        "case ast::SyntaxKind::MatchArmStmt:\n      return DefinitionActivation::MatchPattern;",
        "classification == SkeletonEligibility::Pattern && !patternScope",
        "classification != SkeletonEligibility::Pattern && !skeletonScope",
        "classification == SkeletonEligibility::Pattern ||",
        "classification == SkeletonEligibility::Pattern\n"
        "              ? ZC_ASSERT_NONNULL(patternActivationValue)",
    ):
        if required not in skeleton:
            errors.append(f"{SKELETON_SOURCE}: pattern activation contract is disconnected: {required}")
    if "BindingActivation.PublishesMatchAndLoopPatternFacts" not in tests:
        errors.append(f"{TEST_SOURCE}: missing pattern activation evidence")


def check_definition_site_contract(files: dict[Path, str], errors: list[str]) -> None:
    site_header = files.get(SITE_HEADER, "")
    inventory_header_path = BINDER_DIR / "definition-inventory.h"
    inventory_source_path = BINDER_DIR / "definition-inventory.cc"
    inventory_header = files.get(inventory_header_path, "")
    inventory_source = files.get(inventory_source_path, "")
    frozen_header = files.get(INVENTORY_HEADER, "")
    frozen_source = files.get(INVENTORY_SOURCE, "")
    for required in (
        "struct PatternBindingSite final",
        "static DefinitionSite pattern(ast::NodeId introducer,",
        "DefinitionSite clone() const;",
    ):
        if required not in site_header:
            errors.append(f"{SITE_HEADER}: incomplete definition-site provenance: {required}")
    for path, text in ((inventory_header_path, inventory_header),
                       (INVENTORY_HEADER, frozen_header)):
        if "DefinitionSite site;" not in text:
            errors.append(f"{path}: definition inventory drops exact DefinitionSite")
    for required in (
        "addPatternBinding(",
        "DefinitionSite::pattern(introducer, zc::mv(path))",
        "ast::kVariableDeclaratorPatternWord]), declarator,",
        "ast::kMatchArmStmtPatternWord]",
        "ast::kForInStatementBindingWord]",
    ):
        if required not in inventory_source:
            errors.append(f"{inventory_source_path}: pattern site is incomplete: {required}")
    if "entry.node, entry.site.clone(), definitionValue" not in frozen_source:
        errors.append(f"{INVENTORY_SOURCE}: frozen inventory drops DefinitionSite provenance")


def check_private_binding_candidate(files: dict[Path, str], errors: list[str]) -> None:
    body = type_body(files.get(VERIFIER_HEADER, ""), "BindingMetadataCandidate")
    private = body.find("private:")
    constructor = "BindingMetadataCandidate(identity::SemanticContextBrand semanticContext,"
    if private < 0 or body.find(constructor, private) < 0:
        errors.append(f"{VERIFIER_HEADER}: BindingMetadataCandidate constructor must be private")
    public = body[:private] if private >= 0 else body
    if re.search(r"\bBindingMetadataCandidate\s*\((?!BindingMetadataCandidate&&)", public):
        errors.append(f"{VERIFIER_HEADER}: BindingMetadataCandidate exposes public construction")


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
    forbidden_includes = (
        '"zomlang/compiler/binder/binding-input.h"',
        '"zomlang/compiler/binder/internal/binding-verifier.h"',
    )
    for path, text in files.items():
        if any(root == path or root in path.parents for root in FORBIDDEN_INCLUDE_ROOTS):
            for include in forbidden_includes:
                if include in text:
                    errors.append(f"{path}: checker/irgen/symbol cannot include {include}")
        if "BindingInputCandidate" not in text or path == HEADER:
            continue
        allowed = BINDER_DIR in path.parents or Path("products/zomlang/compiler/driver") in path.parents
        allowed = allowed or Path("products/zomlang/tests") in path.parents
        if not allowed:
            errors.append(f"{path}: BindingInputCandidate escaped binder/driver/tests")


def check_internal_binding_authority(files: dict[Path, str], errors: list[str]) -> None:
    internal_include = '"zomlang/compiler/binder/internal/binding-verifier.h"'
    for path, text in files.items():
        if path == VERIFIER_HEADER or path == VERIFIER_SOURCE or TEST_DIR in path.parents:
            continue
        if internal_include in text:
            errors.append(f"{path}: binder-internal verifier header escaped")
        for symbol in (
            "BindingBuilder::build(",
            "BindingVerifier::verify(",
        ):
            if symbol in text:
                errors.append(f"{path}: binder-internal authority escaped through {symbol}")


def check_scope_arena_contract(files: dict[Path, str], errors: list[str]) -> None:
    header = files.get(SCOPE_HEADER, "")
    source = files.get(SCOPE_SOURCE, "")
    internal_include = '"zomlang/compiler/binder/internal/scope-arena.h"'
    for path, text in files.items():
        if path in {SCOPE_HEADER, SCOPE_SOURCE, SKELETON_HEADER, SKELETON_SOURCE,
                    VERIFIER_SOURCE} or TEST_DIR in path.parents:
            continue
        if internal_include in text:
            errors.append(f"{path}: scope arena internal authority escaped")
    for forbidden in ("zc::HashMap", "ScopeManager", "ast::BindingMetadata", "const_cast"):
        if forbidden in header or forbidden in source:
            errors.append(f"{SCOPE_SOURCE}: forbidden scope allocation dependency: {forbidden}")
    for required in (
        "ScopeArenaCandidate",
        "ScopeArenaBuilder",
        "checkedScopeIndex(uint64_t value)",
    ):
        if required not in header:
            errors.append(f"{SCOPE_HEADER}: incomplete scope arena contract: {required}")
    for required in (
        "ast::visitChildNodeIds(",
        "rootSpan()",
        "spanFor(",
        "definitionAt(node)",
        "implAt(node)",
        "checkedScopeIndex(nextScopeIndex)",
        "SyntaxKind::ExternDecl",
        "case ast::SyntaxKind::LambdaExpression:\n      return ScopeKind::Closure;",
        "SyntaxKind::ErrorDecl",
        "SyntaxKind::MarkerImpl",
        "SyntaxKind::DoWhileStatement",
        "SyntaxKind::MatchArmStmt",
        "SyntaxKind::UnsafeBlockExpr",
    ):
        if required not in source:
            errors.append(f"{SCOPE_SOURCE}: incomplete deterministic scope allocation: {required}")
    for marker in (
        "ScopeArena.AllocatesStructuralScopesInSchemaPreorder",
        "ScopeArena.AssignsDefinitionAndImplOwners",
        "ScopeArena.RejectsScopeIndexOverflow",
    ):
        if marker not in files.get(TEST_SOURCE, ""):
            errors.append(f"{TEST_SOURCE}: missing scope arena evidence: {marker}")
    verifier = files.get(VERIFIER_SOURCE, "")
    for required in (
        "ScopeArenaBuilder::build(input)",
        "encodeImplementation(",
        "encoder.encodeUint8(0x03)",
    ):
        if required not in verifier:
            errors.append(f"{VERIFIER_SOURCE}: scope arena cutover is disconnected: {required}")
    for forbidden in ("isUnsupportedScopeProducer", "ScopeId(input.module(), 1)"):
        if forbidden in verifier:
            errors.append(f"{VERIFIER_SOURCE}: restricted scope construction remains: {forbidden}")
    for path, text in files.items():
        if path in {METADATA_SOURCE, SCOPE_SOURCE} or TEST_DIR in path.parents:
            continue
        if re.search(r"\bScopeId(?:\s+\w+)?\s*\(\s*input\.module\(\)", text):
            errors.append(f"{path}: ScopeId construction escaped the scope arena")
    if "friend class BindingBuilder;" in type_body(files.get(METADATA_HEADER, ""), "ScopeId"):
        errors.append(f"{METADATA_HEADER}: BindingBuilder retains ScopeId construction authority")


def check_binding_skeleton_contract(files: dict[Path, str], errors: list[str]) -> None:
    header = files.get(SKELETON_HEADER, "")
    source = files.get(SKELETON_SOURCE, "")
    internal_include = '"zomlang/compiler/binder/internal/binding-skeleton.h"'
    for path, text in files.items():
        if path in {SKELETON_HEADER, SKELETON_SOURCE, VERIFIER_SOURCE} or TEST_DIR in path.parents:
            continue
        if internal_include in text:
            errors.append(f"{path}: binding skeleton internal authority escaped")
    for forbidden in ("zc::HashMap", "ScopeManager", "ast::BindingMetadata", "const_cast",
                      "switch (kind) default:"):
        if forbidden in header or forbidden in source:
            errors.append(f"{SKELETON_SOURCE}: forbidden skeleton dependency: {forbidden}")
    for required in (
        "class BindingSkeletonBuilder final",
        "DefinitionSkeletonBuildResult build(const VerifiedBindingInput& input,",
        "zc::Vector<DefinitionFact> definitions;",
        "zc::Vector<ImplBindingFact> impls;",
        "zc::Vector<SkeletonDuplicateFact> duplicates;",
        "zc::Vector<ModuleSkeletonSurfaceSeed> moduleSurfaceSeeds;",
    ):
        if required not in header:
            errors.append(f"{SKELETON_HEADER}: incomplete skeleton contract: {required}")
    for required in (
        "SkeletonEligibility eligibility(identity::DefinitionKind kind)",
        "return SkeletonEligibility::Generic;",
        "return SkeletonEligibility::Parameter;",
        "case DefinitionKind::ReexportAlias:",
        "ast::kEnumDeclarationTypeParamsIdWord",
        "ast::kFunctionExpressionTypeParamsIdWord",
        "ast::kStandaloneImplDeclTypeParamsIdWord",
        "ast::kMarkerImplTypeParamsIdWord",
        "ast::kFunctionDeclTypeParamsIdWord",
        "ast::kClassDeclTypeParamsIdWord",
        "ast::kStructDeclTypeParamsIdWord",
        "ast::kInterfaceDeclTypeParamsIdWord",
        "ast::kMethodDeclTypeParamsIdWord",
        "genericOwner(input.tree(), definition.node, ambiguousOwner)",
        "ast::kExternDeclParamsFirstWord",
        "ast::kFunctionDeclParamsIdWord",
        "ast::kMethodDeclParamsIdWord",
        "parameterOwner(input.tree(), definition.node, ambiguousOwner)",
        "inventory[current].key.encode()",
        "definition.site.clone()",
        "DefinitionActivation::ModuleSkeleton",
        "DefinitionActivation::GenericList",
        "DefinitionActivation::ParameterList",
        "classification != SkeletonEligibility::Generic &&\n"
        "          classification != SkeletonEligibility::Parameter && record.kind == ScopeKind::Module",
        "sortBindings(scope.bindings)",
        "sortSurfaceSeeds(result.moduleSurfaceSeeds)",
        "implementations[current].key.encode()",
        "definition.declaringScope == scopeValue",
        "definition.activation == DefinitionActivation::ModuleSkeleton",
        "result.impls.add(ImplBindingFact",
        "declarationExport(input.tree(), definition.node, ambiguousExport)",
        "exportNode != zc::none",
        "redeclarationCode(factValue.kind)",
        "scope.bindings = zc::mv(unique)",
        "isRejected(result, seed.identity)",
    ):
        if required not in source:
            errors.append(f"{SKELETON_SOURCE}: incomplete skeleton projection: {required}")
    if "BindingSkeletonBuilder::build(input, arena)" not in files.get(VERIFIER_SOURCE, ""):
        errors.append(f"{VERIFIER_SOURCE}: binding skeleton cutover is disconnected")
    for required in (
        "encodeSequenceSize(candidate.impls.size())",
        "encodeImplementation(encoder, input, fact.identity)",
        "candidate.impls.size() < expected.impls.size()",
        "candidate.impls.size() > expected.impls.size()",
        "BindingDiagnosticAdapter::emitRedeclaration(",
        "candidate.sourceFailures = zc::mv(sourceFailures)",
    ):
        if required not in files.get(VERIFIER_SOURCE, ""):
            errors.append(f"{VERIFIER_SOURCE}: impl fact verification is disconnected: {required}")
    for marker in (
        "BindingSkeleton.PublishesModuleAndTypeFactsInCanonicalMaps",
        "BindingActivation.PublishesImplMembersAndNamedParameters",
        "BindingSkeleton.IncludesModuleConstantPatternLeaves",
        "BindingSkeleton.PublishesOnlyDeclarationExports",
        "BindingSkeleton.PublishesEmptyMarkerImplFact",
        "BindingVerifier.RejectsMalformedImplFactsAndMemberOrder",
        "BindingSkeleton.RejectsDuplicateFunctionsAsSourceFailures",
        "BindingSkeleton.RejectsNfcEquivalentFunctionNames",
        "BindingSkeleton.UsesKindSpecificRedeclarationCodes",
        "BindingActivation.PublishesScopeOwningGenericLists",
        "BindingActivation.RejectsDuplicateGenericParameters",
        "BindingActivation.PublishesNamedCallableParameterLists",
        "BindingActivation.RejectsDuplicateNamedParameters",
        "BindingBuilder.DefersIdentifierResolutionBeforePublishingMetadata",
    ):
        if marker not in files.get(TEST_SOURCE, ""):
            errors.append(f"{TEST_SOURCE}: missing binding skeleton evidence: {marker}")


def check_binding_diagnostic_adapter(files: dict[Path, str], errors: list[str]) -> None:
    header = files.get(DIAGNOSTIC_ADAPTER_HEADER, "")
    source = files.get(DIAGNOSTIC_ADAPTER_SOURCE, "")
    definitions = files.get(DIAGNOSTIC_DEFINITIONS, "")
    for required in (
        "class VerifiedIdentifierArgument final",
        "const identity::SemanticIdentifier& identifier",
        "class BindingDiagnosticAdapter final",
        "VerifiedIdentifierArgument&& identifier",
    ):
        if required not in header:
            errors.append(f"{DIAGNOSTIC_ADAPTER_HEADER}: incomplete typed adapter: {required}")
    for required in (
        "VerifiedIdentifierArgument::from(",
        "diagnostics::DiagID::PreviousDeclarationHere",
        "case BinderDiagnosticCode::DuplicateIdentifier:",
    ):
        if required not in source:
            errors.append(f"{DIAGNOSTIC_ADAPTER_SOURCE}: incomplete redeclaration adapter: {required}")
    if 'DIAG(3017, PreviousDeclarationHere, kNote, "Previous declaration is here", 0)' not in definitions:
        errors.append(f"{DIAGNOSTIC_DEFINITIONS}: missing ZOM3017 previous declaration note")
    if "BindingDiagnosticAdapter.EmitsTypedRedeclarationWithPreviousNote" not in files.get(TEST_SOURCE, ""):
        errors.append(f"{TEST_SOURCE}: missing typed redeclaration adapter evidence")


def check_wiring(files: dict[Path, str], errors: list[str]) -> None:
    required = (
        (BINDER_CMAKE, "${CMAKE_CURRENT_SOURCE_DIR}/binding-input.cc"),
        (BINDER_CMAKE, "${CMAKE_CURRENT_SOURCE_DIR}/parsed-module.cc"),
        (BINDER_CMAKE, "${CMAKE_CURRENT_SOURCE_DIR}/frozen-definition-inventory.cc"),
        (BINDER_CMAKE, "${CMAKE_CURRENT_SOURCE_DIR}/definition-site.cc"),
        (BINDER_CMAKE, "${CMAKE_CURRENT_SOURCE_DIR}/binding-metadata.cc"),
        (BINDER_CMAKE, "${CMAKE_CURRENT_SOURCE_DIR}/binding-verifier.cc"),
        (BINDER_CMAKE, "${CMAKE_CURRENT_SOURCE_DIR}/scope-arena.cc"),
        (BINDER_CMAKE, "${CMAKE_CURRENT_SOURCE_DIR}/binding-skeleton.cc"),
        (BINDER_CMAKE, "${CMAKE_CURRENT_SOURCE_DIR}/binding-diagnostic-adapter.cc"),
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


def check_invariant_diagnostics(files: dict[Path, str], errors: list[str]) -> None:
    source = files.get(METADATA_SOURCE, "")
    definitions = files.get(DIAGNOSTIC_DEFINITIONS, "")
    mappings = (
        ("MalformedScopeGraph", "BinderMalformedScopeGraph", 9922),
        ("MissingRequiredResolution", "BinderMissingRequiredResolution", 9923),
        ("AliasCycle", "BinderAliasCycle", 9924),
        ("InvalidBindingFact", "BinderInvalidFact", 9925),
        ("InvalidEmitterOrdinal", "BinderInvalidEmitterOrdinal", 9926),
    )
    for kind, diagnostic, code in mappings:
        marker = f"case BinderInvariantKind::{kind}:\n      return DiagID::{diagnostic};"
        if marker not in source:
            errors.append(f"{METADATA_SOURCE}: missing exhaustive {kind} diagnostic mapping")
        if f"DIAG({code}, {diagnostic}, kFatal," not in definitions:
            errors.append(f"{DIAGNOSTIC_DEFINITIONS}: missing registered ZOM{code} diagnostic")
    for forbidden in ("throw ", "ZC_FAIL_REQUIRE", "ZC_IREQUIRE"):
        if forbidden in files.get(VERIFIER_SOURCE, ""):
            errors.append(f"{VERIFIER_SOURCE}: typed verifier failure escaped through {forbidden}")


def check_binding_publication_contract(files: dict[Path, str], errors: list[str]) -> None:
    metadata = files.get(METADATA_HEADER, "")
    verifier = files.get(VERIFIER_HEADER, "")
    source = files.get(VERIFIER_SOURCE, "")
    for forbidden in (
        "BindingMetadataCandidate",
        "class BindingBuilder final",
        "BindingVerifier final",
    ):
        if forbidden in metadata:
            errors.append(f"{METADATA_HEADER}: mutable verifier authority escaped through {forbidden}")
    for required in (
        "class SourceRejected final",
        "class InvariantRejected final",
        "zc::OneOf<VerifiedBindingOutput, SourceRejected, InvariantRejected>",
        "zc::OneOf<identity::IdentityInvariant, BinderInvariantFact>",
        "zc::Maybe<diagnostics::DiagnosticEngine&> diagnostics);",
    ):
        if required not in verifier:
            errors.append(f"{VERIFIER_HEADER}: incomplete verification result contract: {required}")
    if not re.search(
        r"BindingCandidateResult\s+build\(const VerifiedBindingInput& input,\s*"
        r"diagnostics::DiagnosticEngine& diagnostics\);",
        verifier,
    ):
        errors.append(f"{VERIFIER_HEADER}: incomplete verified binding builder contract")
    if re.search(r"build\s*\(\s*const VerifiedBindingInput& input\s*\)\s*;", verifier):
        errors.append(f"{VERIFIER_HEADER}: binding candidate construction can omit diagnostics")
    for required in (
        "encodeAllocationScopeRecord(",
        "encodeBindingAllocationDump(",
        "candidateAllocation = encodeBindingAllocationDump(",
        "expectedAllocation = encodeBindingAllocationDump(",
        "BindingSkeletonBuilder::build(input, arena)",
        "buildCandidate(input, zc::none)",
    ):
        if required not in source:
            errors.append(f"{VERIFIER_SOURCE}: binding publication contract is disconnected: {required}")


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
    check_frozen_impl_inventory_contract(files, errors)
    check_special_callable_contract(files, errors)
    check_closure_activation_contract(files, errors)
    check_pattern_activation_contract(files, errors)
    check_definition_site_contract(files, errors)
    check_private_binding_candidate(files, errors)
    check_producer_boundaries(files, errors)
    check_layering(files, errors)
    check_internal_binding_authority(files, errors)
    check_scope_arena_contract(files, errors)
    check_binding_skeleton_contract(files, errors)
    check_binding_diagnostic_adapter(files, errors)
    check_wiring(files, errors)
    check_invariant_diagnostics(files, errors)
    check_binding_publication_contract(files, errors)
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
            "missing frozen impl lookup",
            INVENTORY_HEADER,
            "zc::Maybe<identity::ImplId> implAt(ast::NodeId node) const;",
            "zc::Maybe<identity::ImplId> missingImplAt(ast::NodeId node) const;",
        ),
        (
            "missing frozen impl cardinality",
            INVENTORY_SOURCE,
            "registries.impls().size() != inventory.impls().size()",
            "registries.impls().size() == inventory.impls().size()",
        ),
        (
            "impl compatibility rejection",
            INVENTORY_HEADER,
            "InvalidDefinitionIdentity\n};",
            "InvalidDefinitionIdentity,\n  UnsupportedImplInventory\n};",
        ),
        (
            "missing pattern site factory",
            SITE_HEADER,
            "static DefinitionSite pattern(ast::NodeId introducer,",
            "static DefinitionSite missingPattern(ast::NodeId introducer,",
        ),
        (
            "declaration-only pattern inventory",
            BINDER_DIR / "definition-inventory.cc",
            "DefinitionSite::pattern(introducer, zc::mv(path))",
            "DefinitionSite::declaration(node)",
        ),
        (
            "dropped frozen definition site",
            INVENTORY_SOURCE,
            "entry.node, entry.site.clone(), definitionValue",
            "entry.node, DefinitionSite::declaration(entry.node), definitionValue",
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
        (
            "public metadata constructor",
            METADATA_HEADER,
            "class VerifiedBindingMetadata final {\npublic:",
            "class VerifiedBindingMetadata final {\npublic:\n  explicit VerifiedBindingMetadata(int);",
        ),
        (
            "public surface constructor",
            METADATA_HEADER,
            "class VerifiedExportSurface final {\npublic:",
            "class VerifiedExportSurface final {\npublic:\n  explicit VerifiedExportSurface(int);",
        ),
        (
            "foreign metadata publication",
            Path("products/zomlang/compiler/checker/escape.cc"),
            "",
            "VerifiedBindingMetadata(value);",
        ),
        (
            "foreign surface publication",
            Path("products/zomlang/compiler/irgen/escape.cc"),
            "",
            "VerifiedExportSurface(value);",
        ),
        (
            "public binding candidate",
            VERIFIER_HEADER,
            "struct BindingMetadataCandidate final {",
            "struct BindingMetadataCandidate final {\n  BindingMetadataCandidate(int);",
        ),
        (
            "forbidden binding facts include",
            Path("products/zomlang/compiler/irgen/escape.cc"),
            "",
            '#include "zomlang/compiler/binder/internal/binding-verifier.h"',
        ),
        (
            "missing facts wiring",
            BINDER_CMAKE,
            "${CMAKE_CURRENT_SOURCE_DIR}/binding-metadata.cc",
            "${CMAKE_CURRENT_SOURCE_DIR}/missing-facts.cc",
        ),
        (
            "missing verifier wiring",
            BINDER_CMAKE,
            "${CMAKE_CURRENT_SOURCE_DIR}/binding-verifier.cc",
            "${CMAKE_CURRENT_SOURCE_DIR}/missing-verifier.cc",
        ),
        (
            "missing binder invariant mapping",
            METADATA_SOURCE,
            "return DiagID::BinderInvalidFact;",
            "return DiagID::BinderMalformedScopeGraph;",
        ),
        (
            "missing binder invariant registration",
            DIAGNOSTIC_DEFINITIONS,
            "DIAG(9926, BinderInvalidEmitterOrdinal, kFatal,",
            "DIAG(9926, MissingBinderInvalidEmitterOrdinal, kFatal,",
        ),
        (
            "raw verifier failure",
            VERIFIER_SOURCE,
            "",
            '\n  throw "raw verifier failure";\n',
        ),
        (
            "foreign binding builder call",
            Path("products/zomlang/compiler/lexer/escape.cc"),
            "",
            "BindingBuilder::build(input);",
        ),
        (
            "missing scope arena wiring",
            BINDER_CMAKE,
            "${CMAKE_CURRENT_SOURCE_DIR}/scope-arena.cc",
            "${CMAKE_CURRENT_SOURCE_DIR}/missing-scope-arena.cc",
        ),
        (
            "missing binding skeleton wiring",
            BINDER_CMAKE,
            "${CMAKE_CURRENT_SOURCE_DIR}/binding-skeleton.cc",
            "${CMAKE_CURRENT_SOURCE_DIR}/missing-binding-skeleton.cc",
        ),
        (
            "missing typed binder diagnostic wiring",
            BINDER_CMAKE,
            "${CMAKE_CURRENT_SOURCE_DIR}/binding-diagnostic-adapter.cc",
            "${CMAKE_CURRENT_SOURCE_DIR}/missing-binding-diagnostic-adapter.cc",
        ),
        (
            "missing previous declaration note",
            DIAGNOSTIC_DEFINITIONS,
            'DIAG(3017, PreviousDeclarationHere, kNote, "Previous declaration is here", 0)',
            'DIAG(3017, MissingPreviousDeclarationHere, kNote, "Previous declaration is here", 0)',
        ),
        (
            "disconnected binding skeleton cutover",
            VERIFIER_SOURCE,
            "BindingSkeletonBuilder::build(input, arena)",
            "disconnectedBindingSkeleton(input, arena)",
        ),
        (
            "numeric definition ordering",
            SKELETON_SOURCE,
            "inventory[current].key.encode()",
            "zc::heapArray<uint8_t>(0)",
        ),
        (
            "numeric implementation ordering",
            SKELETON_SOURCE,
            "implementations[current].key.encode()",
            "zc::heapArray<uint8_t>(0)",
        ),
        (
            "disconnected direct impl members",
            SKELETON_SOURCE,
            "definition.activation == DefinitionActivation::ModuleSkeleton",
            "definition.activation != DefinitionActivation::ModuleSkeleton",
        ),
        (
            "deferred generic parameter activation",
            SKELETON_SOURCE,
            "case DefinitionKind::TypeParameter:\n      return SkeletonEligibility::Generic;",
            "case DefinitionKind::TypeParameter:\n      return SkeletonEligibility::Deferred;",
        ),
        (
            "deferred named parameter activation",
            SKELETON_SOURCE,
            "case DefinitionKind::Parameter:\n      return SkeletonEligibility::Parameter;",
            "case DefinitionKind::Parameter:\n      return SkeletonEligibility::Deferred;",
        ),
        (
            "special callable inventory rejection",
            INVENTORY_SOURCE,
            "bindingName == zc::none && !permitsAbsentLexicalBinding(entry.kind)",
            "bindingName == zc::none",
        ),
        (
            "unbounded special callable identity admission",
            INVENTORY_SOURCE,
            "return kind == identity::DefinitionKind::Constructor ||\n"
            "         kind == identity::DefinitionKind::Destructor;",
            "return true;",
        ),
        (
            "deferred special callable activation",
            SKELETON_SOURCE,
            "case DefinitionKind::Constructor:\n"
            "    case DefinitionKind::Destructor:\n"
            "      return SkeletonEligibility::SpecialCallable;",
            "case DefinitionKind::Constructor:\n"
            "    case DefinitionKind::Destructor:\n"
            "      return SkeletonEligibility::Deferred;",
        ),
        (
            "special callable lexical binding leak",
            SKELETON_SOURCE,
            "const bool lexicalBinding = hasLexicalBinding(classification);",
            "const bool lexicalBinding = true;",
        ),
        (
            "deferred closure activation",
            SKELETON_SOURCE,
            "case DefinitionKind::Closure:\n      return SkeletonEligibility::Closure;",
            "case DefinitionKind::Closure:\n      return SkeletonEligibility::Deferred;",
        ),
        (
            "closure lexical binding leak",
            SKELETON_SOURCE,
            "return classification != SkeletonEligibility::SpecialCallable &&\n"
            "         classification != SkeletonEligibility::Closure;",
            "return true;",
        ),
        (
            "missing closure parameter payload",
            SKELETON_SOURCE,
            "ast::kFunctionExpressionParamsIdWord",
            "ast::kMissingFunctionExpressionParamsIdWord",
        ),
        (
            "missing closure expression activation",
            SKELETON_SOURCE,
            "DefinitionActivation::ExpressionIntroduction",
            "DefinitionActivation::ModuleSkeleton",
        ),
        (
            "generic parameter module surface leak",
            SKELETON_SOURCE,
            "classification != SkeletonEligibility::Generic &&\n"
            "          classification != SkeletonEligibility::Parameter && record.kind == ScopeKind::Module",
            "record.kind == ScopeKind::Module",
        ),
        (
            "missing generic owner validation",
            SKELETON_SOURCE,
            "genericOwner(input.tree(), definition.node, ambiguousOwner)",
            "zc::Maybe<ast::NodeId> owner;",
        ),
        (
            "missing parameter owner validation",
            SKELETON_SOURCE,
            "parameterOwner(input.tree(), definition.node, ambiguousOwner)",
            "zc::Maybe<ast::NodeId> owner;",
        ),
        (
            "missing generic activation evidence",
            TEST_SOURCE,
            "BindingActivation.PublishesScopeOwningGenericLists",
            "BindingActivation.MissingScopeOwningGenericLists",
        ),
        (
            "missing parameter activation evidence",
            TEST_SOURCE,
            "BindingActivation.PublishesNamedCallableParameterLists",
            "BindingActivation.MissingNamedCallableParameterLists",
        ),
        (
            "missing special callable activation evidence",
            TEST_SOURCE,
            "BindingActivation.PublishesSpecialCallableParameterLists",
            "BindingActivation.MissingSpecialCallableParameterLists",
        ),
        (
            "missing closure activation evidence",
            TEST_SOURCE,
            "BindingActivation.PublishesClosureIdentityAndParameters",
            "BindingActivation.MissingClosureIdentityAndParameters",
        ),
        (
            "deferred pattern activation",
            SKELETON_SOURCE,
            "case DefinitionKind::PatternBinding:\n      return SkeletonEligibility::Pattern;",
            "case DefinitionKind::PatternBinding:\n      return SkeletonEligibility::Deferred;",
        ),
        (
            "missing loop pattern activation",
            SKELETON_SOURCE,
            "case ast::SyntaxKind::ForInStatement:\n      return DefinitionActivation::LoopPattern;",
            "case ast::SyntaxKind::ForInStatement:\n      return DefinitionActivation::ModuleSkeleton;",
        ),
        (
            "missing match pattern activation",
            SKELETON_SOURCE,
            "case ast::SyntaxKind::MatchArmStmt:\n      return DefinitionActivation::MatchPattern;",
            "case ast::SyntaxKind::MatchArmStmt:\n      return DefinitionActivation::ModuleSkeleton;",
        ),
        (
            "pattern scope bypass",
            SKELETON_SOURCE,
            "classification == SkeletonEligibility::Pattern && !patternScope",
            "classification == SkeletonEligibility::Pattern && false",
        ),
        (
            "missing pattern activation evidence",
            TEST_SOURCE,
            "BindingActivation.PublishesMatchAndLoopPatternFacts",
            "BindingActivation.MissingMatchAndLoopPatternFacts",
        ),
        (
            "missing impl fact codec",
            VERIFIER_SOURCE,
            "encodeImplementation(encoder, input, fact.identity)",
            "encodeDefinition(encoder, input, fact.identity)",
        ),
        (
            "duplicate overwrite path",
            SKELETON_SOURCE,
            "scope.bindings = zc::mv(unique)",
            "scope.bindings = zc::Vector<ScopeBindingEntry>()",
        ),
        (
            "raw duplicate diagnostics",
            VERIFIER_SOURCE,
            "BindingDiagnosticAdapter::emitRedeclaration(",
            "engine.diagnose<diagnostics::DiagID::RedeclareFunction>(",
        ),
        (
            "foreign scope arena include",
            Path("products/zomlang/compiler/checker/escape.cc"),
            "",
            '#include "zomlang/compiler/binder/internal/scope-arena.h"',
        ),
        (
            "missing closure scope producer",
            SCOPE_SOURCE,
            "case ast::SyntaxKind::LambdaExpression:\n      return ScopeKind::Closure;",
            "case ast::SyntaxKind::LambdaExpression:\n      return zc::none;",
        ),
        (
            "disconnected scope source validation",
            SCOPE_SOURCE,
            "auto span = input.parsedModule().spanFor(tree.node(node).range);",
            "zc::Maybe<identity::SourceSpan> span;",
        ),
        (
            "disconnected scope arena cutover",
            VERIFIER_SOURCE,
            "ScopeArenaBuilder::build(input)",
            "disconnectedScopeArena(input)",
        ),
        (
            "restored hard-coded scope construction",
            VERIFIER_SOURCE,
            "auto arenaResult = ScopeArenaBuilder::build(input);",
            "const ScopeId escaped(input.module(), 1);\n"
            "  auto arenaResult = ScopeArenaBuilder::build(input);",
        ),
        (
            "missing impl scope owner codec",
            VERIFIER_SOURCE,
            "encoder.encodeUint8(0x03);",
            "encoder.encodeUint8(0x02);",
        ),
        (
            "diagnostic-free binding builder",
            VERIFIER_HEADER,
            "const VerifiedBindingInput& input,\n"
            "                                                   diagnostics::DiagnosticEngine& diagnostics);",
            "const VerifiedBindingInput& input);",
        ),
        (
            "public builder authority",
            METADATA_HEADER,
            "class VerifiedBindingMetadata final",
            "class BindingBuilder final {};\nclass VerifiedBindingMetadata final",
        ),
        (
            "incomplete verification algebra",
            VERIFIER_HEADER,
            "zc::OneOf<VerifiedBindingOutput, SourceRejected, InvariantRejected>",
            "zc::OneOf<VerifiedBindingOutput, InvariantRejected>",
        ),
        (
            "disconnected allocation verifier",
            VERIFIER_SOURCE,
            "candidateAllocation = encodeBindingAllocationDump(",
            "candidateAllocation = disconnectedBindingAllocationDump(",
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
    allowed = dict(files)
    allowed[Path("products/zomlang/compiler/checker/allowed-metadata.cc")] = (
        '#include "zomlang/compiler/binder/binding-metadata.h"\n'
        "void consume(const VerifiedBindingMetadata& metadata) { (void)metadata.module(); }\n"
    )
    if check(allowed):
        failures.append("self-test positive fixture rejected: checker metadata consumption")
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
