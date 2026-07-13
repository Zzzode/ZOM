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
PARSER_HEADER = Path("products/zomlang/compiler/parser/parser.h")
PARSER_SOURCE = Path("products/zomlang/compiler/parser/parser.cc")
TOKEN_SNAPSHOT_HEADER = Path("products/zomlang/compiler/parser/token-snapshot.h")
TOKEN_CURSOR_HEADER = Path("products/zomlang/compiler/parser/token-cursor.h")
INVENTORY_HEADER = BINDER_DIR / "frozen-definition-inventory.h"
INVENTORY_SOURCE = BINDER_DIR / "frozen-definition-inventory.cc"
FROZEN_REGISTRY_HEADER = Path("products/zomlang/compiler/identity/frozen-registry.h")
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
BODY_HEADER = BINDER_DIR / "internal" / "body-binding.h"
BODY_SOURCE = BINDER_DIR / "body-binding.cc"
LABEL_HEADER = BINDER_DIR / "internal" / "label-facts.h"
LABEL_SOURCE = BINDER_DIR / "label-facts.cc"
CONTROL_HEADER = BINDER_DIR / "internal" / "control-transfer.h"
CONTROL_SOURCE = BINDER_DIR / "control-transfer.cc"
AST_TREE_HEADER = Path("products/zomlang/compiler/ast/tree.h")
AST_TREE_SOURCE = Path("products/zomlang/compiler/ast/tree.cc")
DIAGNOSTIC_ADAPTER_HEADER = BINDER_DIR / "binding-diagnostic-adapter.h"
DIAGNOSTIC_ADAPTER_SOURCE = BINDER_DIR / "binding-diagnostic-adapter.cc"
DIAGNOSTIC_DEFINITIONS = Path("products/zomlang/compiler/diagnostics/diagnostics-binder.def")
BINDER_CMAKE = BINDER_DIR / "CMakeLists.txt"
TEST_DIR = Path("products/zomlang/tests/unittests/compiler/binder")
TEST_SOURCE = TEST_DIR / "binding-input-test.cc"
TEST_CMAKE = TEST_DIR / "CMakeLists.txt"
DIAGNOSTIC_ADAPTER_TEST = TEST_DIR / "binding-diagnostic-adapter-test.cc"
PARSER_TEST_SOURCE = Path("products/zomlang/tests/unittests/compiler/parser/parser-test.cc")
FROZEN_REGISTRY_TEST = Path(
    "products/zomlang/tests/unittests/compiler/identity/frozen-registry-test.cc"
)
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
        BODY_HEADER,
        BODY_SOURCE,
        LABEL_HEADER,
        LABEL_SOURCE,
        CONTROL_HEADER,
        CONTROL_SOURCE,
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


def function_body(text: str, signature: str) -> str:
    start = text.find(signature)
    if start < 0:
        return ""
    brace = text.find("{", start + len(signature))
    if brace < 0:
        return ""
    depth = 1
    for index in range(brace + 1, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[brace + 1:index]
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


def check_parsed_token_provenance(files: dict[Path, str], errors: list[str]) -> None:
    snapshot = files.get(TOKEN_SNAPSHOT_HEADER, "")
    cursor = files.get(TOKEN_CURSOR_HEADER, "")
    parser_header = files.get(PARSER_HEADER, "")
    parser_source = files.get(PARSER_SOURCE, "")
    parsed_header = files.get(PARSED_HEADER, "")
    parsed_source = files.get(PARSED_SOURCE, "")
    for required in (
        "class ParsedTokenSnapshot final",
        "zc::String canonicalText;",
        "const source::SourceManager* sourceManager;",
        "source::BufferId buffer;",
        "friend class Parser;",
        "friend class binder::ParsedModuleVerifier;",
    ):
        if required not in snapshot:
            errors.append(f"{TOKEN_SNAPSHOT_HEADER}: incomplete parser token capability: {required}")
    if "friend class TokenStream;" in snapshot:
        errors.append(f"{TOKEN_SNAPSHOT_HEADER}: resettable TokenStream can forge parser authority")
    if "copyBufferedTokenRanges() const" not in cursor or "ParsedTokenSnapshot snapshot(" in cursor:
        errors.append(f"{TOKEN_CURSOR_HEADER}: raw token copies must not construct parser authority")
    if "zc::Maybe<ParsedTokenSnapshot> takeTokenSnapshot();" not in parser_header:
        errors.append(f"{PARSER_HEADER}: successful parse cannot publish token provenance")
    for required in (
        "impl->parseSucceeded = true;",
        "impl->tokenSnapshotTaken = true;",
        "if (!impl->parseSucceeded || impl->tokenSnapshotTaken)",
        "ParsedTokenSnapshot(impl->sourceMgr, impl->bufferId,",
    ):
        if required not in parser_source:
            errors.append(f"{PARSER_SOURCE}: parser token capability is not single-use: {required}")
    for required in (
        "parser::ParsedTokenSnapshot&& tokens, ast::Tree&& tree",
        "retainedTokenSpan(",
        "ast::NodeId owner,",
        "uint32_t tokenOrdinal",
        "ast::SyntaxKind expectedKind",
        "sourceLocFor(",
        "const identity::SourceSpan& span",
        "InvalidTokenProvenance",
    ):
        if required not in parsed_header:
            errors.append(f"{PARSED_HEADER}: verified parsed token contract is incomplete: {required}")
    for required in (
        "tokens.sourceManager != &sources || tokens.buffer != buffer",
        "admitTokenOffsets(tokens.tokenValues.asPtr(), sourceBytes)",
        "token.kind == ast::SyntaxKind::EndOfFile",
        "token.kind == ast::SyntaxKind::Unknown || start == end",
        "static_cast<size_t>(tokenOrdinal) >= impl->tokens.size() - first",
        "token.kind != expectedKind",
        "token.end > span.byteEnd()",
        "!span.belongsTo(impl->snapshot.source())",
        "span.byteEnd() > impl->snapshot.bytes().size()",
        "impl->sources.getLocForOffset(impl->buffer",
    ):
        if required not in parsed_source:
            errors.append(f"{PARSED_SOURCE}: parsed token validation is incomplete: {required}")
    for marker in (
        "ParsedModule.RetainsExactEscapedKeywordTokenSpans",
        "ParsedModule.RetainsLabelTokenOrdinalsAndExactSourceLocations",
        "ParsedModule.RejectsInvalidRetainedTokenAndSourceQueries",
        "ParsedModule.RejectsIdentifierPrefixesAsKeywordProvenance",
    ):
        if marker not in files.get(TEST_SOURCE, ""):
            errors.append(f"{TEST_SOURCE}: missing retained token evidence: {marker}")
    for marker in (
        "ParserTest.TokenSnapshotIsSingleUseAfterSuccessfulParse",
        "ParserTest.FailedParseCannotPublishTokenSnapshot",
    ):
        if marker not in files.get(PARSER_TEST_SOURCE, ""):
            errors.append(f"{PARSER_TEST_SOURCE}: missing parser capability evidence: {marker}")


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


def check_owned_key_projection_contract(files: dict[Path, str], errors: list[str]) -> None:
    registry = files.get(FROZEN_REGISTRY_HEADER, "")
    inventory = files.get(INVENTORY_SOURCE, "")
    inventory_impl = type_body(inventory, "FrozenDefinitionInventoryView::Impl")
    binder_tests = files.get(TEST_SOURCE, "")
    identity_tests = files.get(FROZEN_REGISTRY_TEST, "")
    for required in (
        "class FrozenKeyIndex final",
        "zc::Maybe<FrozenKeyIndex> snapshotKeys() const",
        "lookupProjectedKey(owner, keys.asPtr(), handle)",
        "handle.slot >= keys.size()",
        "return keys[handle.slot];",
    ):
        if required not in registry:
            errors.append(
                f"{FROZEN_REGISTRY_HEADER}: owned frozen key projection is incomplete: {required}"
            )
    for required in (
        "identity::DefinitionRegistry::FrozenKeyIndex definitionKeys;",
        "identity::ImplRegistry::FrozenKeyIndex implKeys;",
        "registries.definitions().snapshotKeys()",
        "registries.impls().snapshotKeys()",
        "impl->definitionKeys.lookup(definition)",
        "impl->implKeys.lookup(implementation)",
    ):
        if required not in inventory:
            errors.append(f"{INVENTORY_SOURCE}: owned key projection is disconnected: {required}")
    if "SemanticIdentityRegistrySet&" in inventory_impl:
        errors.append(f"{INVENTORY_SOURCE}: frozen inventory view retains registry lifetime")
    if "FrozenInventory.OwnsCanonicalKeyProjection" not in binder_tests:
        errors.append(f"{TEST_SOURCE}: missing owned frozen inventory key evidence")
    if "Frozen key index owns canonical lookup after registry move" not in identity_tests:
        errors.append(f"{FROZEN_REGISTRY_TEST}: missing owned frozen key index evidence")


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


def check_body_binding_contract(files: dict[Path, str], errors: list[str]) -> None:
    header = files.get(BODY_HEADER, "")
    source = files.get(BODY_SOURCE, "")
    skeleton = files.get(SKELETON_SOURCE, "")
    verifier = files.get(VERIFIER_SOURCE, "")
    tests = files.get(TEST_SOURCE, "")
    for required in (
        "class BodyBindingBuilder final",
        "struct BodyBindingCandidate final",
        "using BodyBindingBuildResult = zc::OneOf<BodyBindingCandidate, BinderInvariantFact>;",
        "DefinitionSkeletonCandidate& skeleton",
    ):
        if required not in header:
            errors.append(f"{BODY_HEADER}: incomplete body-binding authority: {required}")
    for required in (
        "BinderEmitterSite::BodyBinding",
        "struct ActiveScopeIndex final",
        "zc::HashMap<zc::String, size_t> values;",
        "activeDefinition(scopeIndex, expected, name.text())",
        "BindingResolutionValue(BoundNameResolution",
        "BodyBindingFailureFact{diagnostic, node",
        "ShadowTargetFact{entry.definition",
        "DefinitionActivation::AfterInitializer",
        "DefinitionActivation::ParameterList",
        "DefinitionActivation::LoopPattern",
        "DefinitionActivation::MatchPattern",
        "visitForIn(node, scopeIndex)",
        "visitMatchArm(node, scopeIndex)",
        "resolveIdentifierPath(node, scopeIndex, inherited)",
        "visitCallable(node, scopeIndex)",
        "visitParameterSignature(parameter, scopeIndex)",
        "visitParameterDefaultAndActivate(parameter, scopeIndex)",
        "visitMarkerImpl(node, scopeIndex)",
        "visitTypeQuery(node, scopeIndex)",
        "visitDynTypeMarkers(node, scopeIndex)",
        "visitObjectProperty(node, scopeIndex)",
        "resolveIdentifierPath(path, scopeIndex, Namespace::Value)",
        "resolveIdentifierPath(marker, scopeIndex, Namespace::Type)",
        "resolveName(node, scopeIndex, Namespace::Value, tree.ident(name))",
        "finishDefinitions()",
        "finishNodeBindings()",
        "finishShadowTargets()",
        "BinderDiagnosticCode::RedeclareVariable, BinderEmitterSite::BodyBinding",
    ):
        if required not in source:
            errors.append(f"{BODY_SOURCE}: incomplete source-ordered body binding: {required}")
    initializer_visit = source.find(
        "if (tree.contains(initializer)) { visitNode(initializer, scopeIndex, Namespace::Value); }"
    )
    activation = source.find(
        "activateIntroducer(node, DefinitionActivation::AfterInitializer, true);"
    )
    if initializer_visit < 0 or activation < 0 or initializer_visit > activation:
        errors.append(f"{BODY_SOURCE}: local activation must follow initializer traversal")
    iterable_visit = source.find(
        "if (tree.contains(expression)) { visitNode(expression, scopeIndex, Namespace::Value); }"
    )
    loop_activation = source.find(
        "activateIntroducer(node, DefinitionActivation::LoopPattern, false);"
    )
    if iterable_visit < 0 or loop_activation < 0 or iterable_visit > loop_activation:
        errors.append(f"{BODY_SOURCE}: loop pattern activation must follow iterable traversal")
    parameter_signature = source.find("visitParameterSignature(parameter, scopeIndex);")
    return_type = source.find(
        "if (tree.contains(returnType)) { visitNode(returnType, scopeIndex, Namespace::Type); }"
    )
    parameter_default = source.find("visitParameterDefaultAndActivate(parameter, scopeIndex);")
    callable_body = source.find(
        "if (tree.contains(body)) { visitNode(body, scopeIndex, Namespace::Value); }"
    )
    if (
        parameter_signature < 0
        or return_type < 0
        or parameter_default < 0
        or callable_body < 0
        or not parameter_signature < return_type < parameter_default < callable_body
    ):
        errors.append(f"{BODY_SOURCE}: callable signature/default/body phases are out of order")
    if "if (definition.kind == identity::DefinitionKind::Local) { continue; }" not in skeleton:
        errors.append(f"{SKELETON_SOURCE}: local facts did not leave the module skeleton")
    for required in (
        "BodyBindingBuilder::build(input, arena, skeleton)",
        "bodyResult.is<BodyBindingCandidate>()",
        "BindingDiagnosticAdapter::emitLookupFailure(",
        "BindingResolutionValue(FailedBindingResolution{failureIndex})",
        "candidate.nodeBindings = zc::mv(nodeBindings)",
        "candidate.shadowTargets = zc::mv(body.shadowTargets)",
        "value.is<BoundNameResolution>()",
        "!encodeTarget(encoder, input, bound.canonicalTarget)",
        "encodeSequenceSize(candidate.shadowTargets.size())",
        "hasCompleteLexicalBindingSites(input.tree(), expected.nodeBindings.asPtr())",
        "bodyBuilderFailure(input, BinderInvariantKind::InvalidBindingFact, ordinal)",
        "input.definitions().definitionKey(definition)",
        "case ast::SyntaxKind::TypeQueryExpr:",
        "case ast::SyntaxKind::DynTypeMarkerList:",
        "case ast::SyntaxKind::ObjectProperty:",
        "if (tree.contains(typePath)) {\n          requireSite(typePath, ast::SyntaxKind::ModulePath, Namespace::Type);",
        "requiredNamespaces[binding.node.value]",
    ):
        if required not in verifier:
            errors.append(f"{VERIFIER_SOURCE}: body-binding cutover is disconnected: {required}")
    if "${CMAKE_CURRENT_SOURCE_DIR}/body-binding.cc" not in files.get(BINDER_CMAKE, ""):
        errors.append(f"{BINDER_CMAKE}: body-binding source is not compiled")
    for marker in (
        "BindingActivation.PublishesBlockLocalsAfterInitializers",
        "BindingActivation.RejectsDuplicateBlockLocals",
        "BodyBinding.ResolvesEarlierDeclaratorsAfterActivation",
        "BodyBinding.RejectsSelfReferenceBeforeActivation",
        "BodyBinding.RejectsLaterDeclaratorReference",
        "BodyBinding.RecordsOuterShadowTargetAndResolvesNearestBinding",
        "BodyBinding.ActivatesForInPatternAfterIterable",
        "BodyBinding.ActivatesMatchPatternForGuardAndBody",
        "BodyBinding.OrdersParameterDefaultVisibilityBySource",
        "BodyBinding.RejectsLaterParameterInEarlierDefault",
        "BodyBinding.ReportsValueUseOfTypeNameAsNamespaceMismatch",
        "BodyBinding.ResolvesNamedTypeReferences",
        "BodyBinding.ResolvesTypeQueryPathsInValueNamespace",
        "BodyBinding.ReportsTypeQueryTypeOnlyNamespaceMismatch",
        "BodyBinding.RejectsUnverifiedQualifiedTypeQueryPaths",
        "BodyBinding.ResolvesDynMarkerPathsInTypeNamespace",
        "BodyBinding.RejectsUndefinedDynMarkerPaths",
        "BodyBinding.RejectsUnverifiedQualifiedDynMarkerPaths",
        "BodyBinding.ResolvesObjectShorthandAndSkipsExplicitKeys",
        "BodyBinding.RejectsUndefinedObjectShorthand",
        "BodyBinding.RejectsUndefinedTypeReferences",
        "BodyBinding.KeepsParametersOutOfLaterParameterTypes",
        "BodyBinding.KeepsParametersOutOfReturnTypes",
        "BodyBinding.ReportsTypeUseOfValueNameAsNamespaceMismatch",
        "BodyBinding.ResolvesStructPatternTypePaths",
        "BodyBinding.AcceptsStructPatternsWithoutTypePaths",
        "BodyBinding.RejectsUnverifiedQualifiedTypePaths",
        "BodyBinding.RejectsNfcEquivalentLocalNames",
        "BodyBinding.OrdersLookupAndDuplicateFailuresBySource",
        "BodyBinding.CanonicalizesSkeletonAndBodyDefinitionFactsTogether",
        "BindingVerifier.RejectsWrongBoundNameTarget",
        "BindingVerifier.RejectsMalformedBoundNameFieldsAndOrder",
        "BindingVerifier.RejectsInvalidFailedResolutionIndex",
        "BindingVerifier.RejectsMalformedShadowFacts",
        "BindingVerifier.RejectsForeignBodyBindingIdentities",
        "BindingVerifier.RejectsMissingShadowTarget",
    ):
        if marker not in tests:
            errors.append(f"{TEST_SOURCE}: missing body-binding evidence: {marker}")

    internal_include = '"zomlang/compiler/binder/internal/body-binding.h"'
    for path, text in files.items():
        if path in {BODY_HEADER, BODY_SOURCE, VERIFIER_SOURCE} or TEST_DIR in path.parents:
            continue
        if internal_include in text or re.search(r"\bBodyBindingBuilder::build\(", text):
            errors.append(f"{path}: body-binding internal authority escaped")


def check_label_fact_contract(files: dict[Path, str], errors: list[str]) -> None:
    metadata = files.get(METADATA_HEADER, "")
    metadata_source = files.get(METADATA_SOURCE, "")
    header = files.get(LABEL_HEADER, "")
    source = files.get(LABEL_SOURCE, "")
    verifier = files.get(VERIFIER_SOURCE, "")
    verifier_header = files.get(VERIFIER_HEADER, "")
    control = files.get(CONTROL_SOURCE, "")
    tests = files.get(TEST_SOURCE, "")

    sealed_types = (
        (
            "LabelOwner",
            (
                "explicit LabelOwner(LabelOwnerValue&& value) noexcept;",
                "static LabelOwner module(identity::ModuleId value);",
                "static LabelOwner callable(identity::DefId value);",
                "LabelOwner clone() const;",
            ),
        ),
        (
            "LabelId",
            (
                "LabelId(LabelOwner&& owner, uint32_t index) noexcept;",
                "LabelOwner ownerValue;",
                "uint32_t indexValue;",
            ),
        ),
        (
            "LabelTarget",
            (
                "explicit LabelTarget(LabelTargetValue&& value) noexcept;",
                "static LabelTarget block(ScopeId scope);",
                "static LabelTarget loop(ScopeId scope);",
            ),
        ),
    )
    for name, private_markers in sealed_types:
        body = type_body(metadata, name)
        private = body.find("private:")
        if not body or private < 0:
            errors.append(f"{METADATA_HEADER}: {name} must be a sealed final type")
            continue
        public = body[:private]
        if re.search(rf"(?<!~)\b{name}\s*\(", public):
            errors.append(f"{METADATA_HEADER}: {name} exposes public construction")
        for required in private_markers:
            if required not in body[private:]:
                errors.append(f"{METADATA_HEADER}: incomplete sealed {name}: {required}")
        friends = re.findall(r"friend class\s+([A-Za-z_][A-Za-z0-9_]*);", body)
        if friends != ["LabelBuilder"]:
            errors.append(f"{METADATA_HEADER}: {name} construction authority is not sole LabelBuilder")

    for required in (
        "using LabelOwnerValue = zc::OneOf<ModuleLabelOwner, CallableLabelOwner>;",
        "using LabelTargetValue = zc::OneOf<BlockLabelTarget, LoopLabelTarget>;",
        "struct LabelFact final",
        "LabelId identity;",
        "identity::SemanticIdentifier name;",
        "LabelOwner owner;",
        "ast::NodeId statement;",
        "LabelTarget target;",
        "identity::SourceSpan source;",
    ):
        if required not in metadata:
            errors.append(f"{METADATA_HEADER}: incomplete canonical label fact: {required}")

    for required in (
        '#include "zomlang/compiler/binder/internal/scope-arena.h"',
        "struct LabelDuplicateFact final",
        "identity::SemanticIdentifier name;",
        "identity::SourceSpan primary;",
        "identity::SourceSpan previous;",
        "uint32_t schemaPreorderOrdinal;",
        "struct LabelFactsCandidate final",
        "zc::Vector<LabelFact> labels;",
        "zc::Vector<LabelDuplicateFact> duplicates;",
        "using LabelFactsBuildResult =",
        "checkedLabelIndex(uint64_t value)",
        "class LabelBuilder final",
        "build(const VerifiedBindingInput& input,",
        "const ScopeArenaCandidate& arena)",
    ):
        if required not in header:
            errors.append(f"{LABEL_HEADER}: incomplete label authority: {required}")
    for forbidden in ("ScopeManager", "ast::BindingMetadata", "const_cast"):
        if forbidden in header or forbidden in source:
            errors.append(f"{LABEL_SOURCE}: forbidden label dependency: {forbidden}")

    internal_include = '"zomlang/compiler/binder/internal/label-facts.h"'
    for path, text in files.items():
        if path in {LABEL_HEADER, LABEL_SOURCE, VERIFIER_SOURCE} or TEST_DIR in path.parents:
            continue
        if internal_include in text or re.search(r"\bLabelBuilder::build\(", text):
            errors.append(f"{path}: label internal authority escaped")
        for factory in (
            "LabelOwner::module(",
            "LabelOwner::callable(",
            "LabelTarget::block(",
            "LabelTarget::loop(",
        ):
            if factory in text and path != METADATA_SOURCE:
                errors.append(f"{path}: sealed label factory escaped LabelBuilder")

    for required in (
        "zc::Maybe<uint32_t> checkedLabelIndex(uint64_t value)",
        "value > static_cast<uint64_t>(UINT32_MAX)",
        "ast::visitTreePreOrder(tree, tree.root()",
        "syntax.kind != ast::SyntaxKind::LabeledStatement",
        "zc::Vector<OwnerCounter> counters;",
        "counters[index].owner == ownerValue",
        "checkedLabelIndex(counters[counterIndex].nextIndex)",
        "++counters[counterIndex].nextIndex;",
        "schemaOrdinals[node.value]",
        "scope.kind == ScopeKind::Function || scope.kind == ScopeKind::Closure",
        "owner.is<DefinitionScopeOwner>()",
        "LabelOwner::callable(owner.get<DefinitionScopeOwner>().definition)",
        "owner.is<ModuleScopeOwner>()",
        "LabelOwner::module(input.module())",
        "target = ast::NodeId(syntax.payload.words[ast::kLabeledStatementStatementWord])",
        "kind == ast::SyntaxKind::BlockStmt && scope.kind == ScopeKind::Block",
        "LabelTarget::block(scope.id)",
        "LabelTarget::loop(scope.id)",
        "kind == ast::SyntaxKind::WhileStmt",
        "kind == ast::SyntaxKind::ForStmt",
        "kind == ast::SyntaxKind::ForInStatement",
        "kind == ast::SyntaxKind::DoWhileStatement",
        "input.parsedModule().retainedTokenSpan(node, 0, ast::SyntaxKind::Identifier)",
        "identity::SemanticIdentifier::fromSource(",
        "prior.owner == ownerValue && prior.name == nameValue",
        "candidate.duplicates.add(LabelDuplicateFact",
        "const LabelId identity(ownerValue.clone(), indexValue);",
        "candidate.labels.add(LabelFact",
        "candidate.labels = zc::mv(sorted);",
    ):
        if required not in source:
            errors.append(f"{LABEL_SOURCE}: incomplete canonical label projection: {required}")
    if source.count("ast::visitTreePreOrder(tree, tree.root()") < 2:
        errors.append(f"{LABEL_SOURCE}: label allocation is not independently schema-preorder")
    if source.count("syntax.kind != ast::SyntaxKind::LabeledStatement") < 2:
        errors.append(f"{LABEL_SOURCE}: nested label target flattening is incomplete")

    if "${CMAKE_CURRENT_SOURCE_DIR}/label-facts.cc" not in files.get(BINDER_CMAKE, ""):
        errors.append(f"{BINDER_CMAKE}: label facts source is not compiled")
    pipeline = function_body(verifier, "BindingCandidateResult BindingBuilder::buildCandidate(")
    pipeline_markers = (
        "ScopeArenaBuilder::build(input)",
        "BindingSkeletonBuilder::build(input, arena)",
        "BodyBindingBuilder::build(input, arena, skeleton)",
        "LabelBuilder::build(input, arena)",
        "ControlTransferBuilder::build(input, arena)",
        "zc::TreeMap<PendingFailureOrderKey, PendingFailureRef>",
    )
    pipeline_positions = [pipeline.find(marker) for marker in pipeline_markers]
    if any(position < 0 for position in pipeline_positions) or pipeline_positions != sorted(
        pipeline_positions
    ):
        errors.append(
            f"{VERIFIER_SOURCE}: label facts must run after body binding and before control transfer"
        )

    for required in (
        "enum class PendingFailureKind : uint8_t { Duplicate, BodyLookup, LabelDuplicate, ControlTransfer };",
        "labelResult.is<LabelFactsCandidate>()",
        "labels.duplicates.size()",
        "PendingFailureRef{PendingFailureKind::LabelDuplicate, index}",
        "static_cast<uint8_t>(BinderEmitterSite::LabelAndClosure)",
        "ordered.value.kind == PendingFailureKind::LabelDuplicate",
        "input.parsedModule().sourceLocFor(duplicate.primary)",
        "input.parsedModule().sourceLocFor(duplicate.previous)",
        "BinderDiagnosticCode::DuplicateIdentifier",
        "BinderDiagnosticCode::PreviousDeclarationHere",
        "candidate.labels = zc::mv(labels.labels)",
    ):
        if required not in verifier:
            errors.append(f"{VERIFIER_SOURCE}: label candidate publication is disconnected: {required}")
    if "zc::Vector<LabelFact> labels;" not in verifier_header:
        errors.append(f"{VERIFIER_HEADER}: label candidate storage is missing")

    label_owner_codec = function_body(verifier, "bool encodeLabelOwner(")
    for required in (
        "value.is<ModuleLabelOwner>()",
        "encoder.encodeUint8(0x01);",
        "input.moduleKey().encode(encoder);",
        "encoder.encodeUint8(0x02);",
        "encodeDefinition(encoder, input, value.get<CallableLabelOwner>().callable)",
    ):
        if required not in label_owner_codec:
            errors.append(f"{VERIFIER_SOURCE}: incomplete label owner codec: {required}")
    codec_contracts = (
        (
            "bool encodeLabelId(",
            ("encodeLabelOwner(encoder, input, identity.owner())", "identity.index()"),
        ),
        (
            "bool encodeLabelTarget(",
            (
                "value.is<BlockLabelTarget>()",
                "encoder.encodeUint8(0x01);",
                "value.get<BlockLabelTarget>().scope",
                "encoder.encodeUint8(0x02);",
                "value.get<LoopLabelTarget>().scope",
            ),
        ),
        (
            "bool encodeLabelFact(",
            (
                "fact.identity.owner() != fact.owner",
                "encodeLabelId(encoder, input, fact.identity)",
                "fact.name.encode(encoder);",
                "encodeLabelOwner(encoder, input, fact.owner)",
                "encoder.encodeUint32(fact.statement.value);",
                "encodeLabelTarget(encoder, input, fact.target)",
                "fact.source.encode(encoder);",
            ),
        ),
        (
            "zc::Maybe<zc::Array<uint8_t>> encodeAllocationLabelRecord(",
            (
                "encodeLabelOwner(encoder, input, fact.owner)",
                "encoder.encodeUint32(fact.identity.index());",
                "fact.name.encode(encoder);",
                "target.is<BlockLabelTarget>() ? 0x01 : 0x02",
                "scope.index() >= scopes.size()",
                "scopes[scope.index()].id != scope",
                "scopes[scope.index()].kind != expectedKind",
                "encoder.encodeUint32(scope.index());",
                "fact.source.encode(encoder);",
            ),
        ),
    )
    for signature, required_markers in codec_contracts:
        body = function_body(verifier, signature)
        for required in required_markers:
            if required not in body:
                errors.append(f"{VERIFIER_SOURCE}: incomplete label codec: {required}")
    candidate_codec = function_body(verifier, "zc::Maybe<zc::Array<uint8_t>> encodeCandidate(")
    for required in (
        "encoder.encodeSequenceSize(candidate.labels.size());",
        "for (const auto& fact : candidate.labels)",
        "encodeLabelFact(encoder, input, fact)",
    ):
        if required not in candidate_codec:
            errors.append(f"{VERIFIER_SOURCE}: candidate label codec is incomplete: {required}")
    allocation_dump = function_body(verifier, "zc::Maybe<zc::Array<uint8_t>> encodeBindingAllocationDump(")
    for required in (
        "for (const auto& fact : labels)",
        "encodeAllocationLabelRecord(input, scopes, fact)",
        "compareCanonicalBytes(previousLabelIdentity.asPtr(), labelIdentity.asPtr()) >= 0",
        "frameBindingAllocationDump(scopeRecords.asPtr(), labelRecords.asPtr())",
    ):
        if required not in allocation_dump:
            errors.append(f"{VERIFIER_SOURCE}: label allocation dump is incomplete: {required}")
    for required in (
        "zc::ArrayPtr<const LabelFact> labels",
        "encodeBindingAllocationDump(input, candidate.scopes.asPtr(), candidate.labels.asPtr())",
        "encodeBindingAllocationDump(input, expected.scopes.asPtr(), expected.labels.asPtr())",
    ):
        if required not in verifier and required not in verifier_header:
            errors.append(f"{VERIFIER_SOURCE}: label allocation verification is disconnected: {required}")
    for required in ("labelRecords.size()", "for (const auto record : labelRecords)"):
        if required not in metadata_source:
            errors.append(f"{METADATA_SOURCE}: label allocation framing is incomplete: {required}")

    foreign_check = function_body(verifier, "bool hasForeignContext(")
    for required in (
        "for (const auto& fact : candidate.labels)",
        "fact.owner.value()",
        "owner.is<ModuleLabelOwner>()",
        "input.definitions().definitionKey(callable)",
        "fact.target.value()",
        "scope.module() != input.module() || !scope.belongsTo(input.semanticContext())",
    ):
        if required not in foreign_check:
            errors.append(f"{VERIFIER_SOURCE}: label foreign-context check is incomplete: {required}")
    source_check = function_body(verifier, "bool hasInvalidSourceRange(")
    for required in (
        "for (const auto& fact : candidate.labels)",
        "if (spanIsInvalid(fact.source)) { return true; }",
    ):
        if required not in source_check:
            errors.append(f"{VERIFIER_SOURCE}: label source-range check is incomplete: {required}")
    for required in (
        "candidate.labels.size() < expected.labels.size()",
        "candidate.labels.size() > expected.labels.size()",
    ):
        if required not in verifier:
            errors.append(f"{VERIFIER_SOURCE}: label fact size check is incomplete: {required}")

    oracle = function_body(verifier, "LabelOracleResult verifyLabelFacts(")
    if not oracle:
        errors.append(f"{VERIFIER_SOURCE}: independent label oracle is missing")
    else:
        if "LabelBuilder::build" in oracle:
            errors.append(f"{VERIFIER_SOURCE}: label oracle reuses LabelBuilder authority")
        for required in (
            "ScopeArenaBuilder::build(input)",
            "ast::visitTreePreOrder(tree, tree.root()",
            "scope.kind == ScopeKind::Function || scope.kind == ScopeKind::Closure",
            "owner.get<DefinitionScopeOwner>().definition",
            "syntax.kind != ast::SyntaxKind::LabeledStatement",
            "ast::kLabeledStatementStatementWord",
            "kind == ast::SyntaxKind::BlockStmt && scope.kind == ScopeKind::Block",
            "oracleLoopKind(kind) && scope.kind == ScopeKind::Loop",
            "retainedTokenSpan(node, 0, ast::SyntaxKind::Identifier)",
            "identity::SemanticIdentifier::fromSource(",
            "sameOracleOwner(counters[index].owner, ownerValue)",
            "counters[counterIndex].nextIndex",
            "oracleLabelLess(current, expected[insertion - 1])",
            "actual.identity.owner() != actual.owner",
            "actual.identity.index() != wanted.index",
            "actual.name != wanted.name",
            "actual.statement != wanted.statement",
            "!sameSpan(actual.source, wanted.source)",
            "BinderDiagnosticCode::DuplicateIdentifier",
            "BinderDiagnosticCode::PreviousDeclarationHere",
            "BinderEmitterSite::LabelAndClosure",
            "consumedFailures",
        ):
            if required not in oracle:
                errors.append(f"{VERIFIER_SOURCE}: incomplete independent label oracle: {required}")
    for required in (
        "const auto expectedLabels = verifyLabelFacts(input, expected);",
        "const auto candidateLabels = verifyLabelFacts(input, candidate);",
    ):
        if required not in verifier:
            errors.append(f"{VERIFIER_SOURCE}: independent label verification is disconnected: {required}")

    if "syntax.kind == ast::SyntaxKind::LabeledStatement" in control:
        errors.append(f"{CONTROL_SOURCE}: non-empty label declarations remain fail-closed")
    for required in (
        "ast::kBreakStmtLabelWord",
        "ast::kContinueStatementLabelWord",
        "if (label != 0)",
        "BinderInvariantKind::MissingRequiredResolution",
        "BinderEmitterSite::LabelAndClosure",
    ):
        if required not in control:
            errors.append(f"{CONTROL_SOURCE}: explicit label references do not fail closed: {required}")
    for path in (AST_TREE_HEADER, AST_TREE_SOURCE):
        text = files.get(path, "")
        for forbidden in ("setLabelTarget(", "BindingMetadata::labelTarget(", "labelTargets"):
            if forbidden in text:
                errors.append(f"{path}: obsolete ast label target side table remains: {forbidden}")
    for path, text in files.items():
        for forbidden in ("BoundLabel", "ExplicitLabelControlTarget", "ContinueTargetNotLoop"):
            if forbidden in text:
                errors.append(f"{path}: premature explicit-label contract is forbidden: {forbidden}")
        if "DIAG(3022," in text:
            errors.append(f"{path}: premature ZOM3022 registration is forbidden")

    for marker in (
        "LabelFacts.ResolvesAllTargetsNestedLabelsAndPreservesControlTransfers",
        "LabelFacts.AllocatesModuleAndCallableOwnerIndicesIndependently",
        "LabelFacts.ReportsSiblingNestedAndNfcEquivalentDuplicates",
        "LabelFacts.RejectsLabelIndexOverflow",
        "BindingAllocationDump.EncodesSchemaBackedLabelRecords",
        "BindingVerifier.RejectsMissingAdditionalReorderedAndMutatedLabels",
        "BindingVerifier.RejectsForeignLabelOwnersAndTargets",
        "BindingVerifier.RejectsMalformedLabelDuplicateFailuresAndDeclarationBindings",
        "ControlTransfer.FailsClosedForExplicitLabels",
    ):
        if marker not in tests:
            errors.append(f"{TEST_SOURCE}: missing label-fact evidence: {marker}")


def check_control_transfer_contract(files: dict[Path, str], errors: list[str]) -> None:
    header = files.get(CONTROL_HEADER, "")
    source = files.get(CONTROL_SOURCE, "")
    verifier = files.get(VERIFIER_SOURCE, "")
    tests = files.get(TEST_SOURCE, "")
    adapter_header = files.get(DIAGNOSTIC_ADAPTER_HEADER, "")
    adapter_source = files.get(DIAGNOSTIC_ADAPTER_SOURCE, "")
    definitions = files.get(DIAGNOSTIC_DEFINITIONS, "")

    for required in (
        '#include "zomlang/compiler/binder/internal/scope-arena.h"',
        "struct ControlTransferFailureFact final",
        "BinderDiagnosticCode diagnostic;",
        "identity::SourceSpan source;",
        "uint32_t schemaPreorderOrdinal;",
        "struct ControlTransferCandidate final",
        "zc::Vector<ControlTransferFact> controlTransfers;",
        "zc::Vector<ControlTransferFailureFact> failures;",
        "using ControlTransferBuildResult =",
        "class ControlTransferBuilder final",
        "build(const VerifiedBindingInput& input,",
        "const ScopeArenaCandidate& arena)",
    ):
        if required not in header:
            errors.append(f"{CONTROL_HEADER}: incomplete control-transfer authority: {required}")
    for forbidden in ("ScopeManager", "ast::BindingMetadata", "const_cast"):
        if forbidden in header or forbidden in source:
            errors.append(f"{CONTROL_SOURCE}: forbidden control-transfer dependency: {forbidden}")

    for required in (
        "ast::visitTreePreOrder(tree, tree.root()",
        "ast::kBreakStmtLabelWord",
        "ast::kContinueStatementLabelWord",
        "input.parsedModule().spanFor(tree.node(node).range)",
        "input.parsedModule().retainedTokenSpan(",
        "node, 0,",
        "ast::SyntaxKind::BreakKeyword : ast::SyntaxKind::ContinueKeyword",
        "scope.kind == ScopeKind::Loop",
        "scope.kind == ScopeKind::Match && isBreak",
        "scope.kind == ScopeKind::Function || scope.kind == ScopeKind::Closure ||",
        "scope.kind == ScopeKind::Module || scope.parent == zc::none",
        "ControlTarget(LoopControlTarget{scope.id})",
        "ControlTarget(MatchControlTarget{scope.id})",
        "BinderDiagnosticCode::BreakTargetNotFound",
        "BinderDiagnosticCode::ContinueTargetNotFound",
        "ControlTransferKind::Break : ControlTransferKind::Continue",
        "zc::TreeMap<uint32_t, size_t> order;",
        "candidate.controlTransfers = zc::mv(sorted);",
    ):
        if required not in source:
            errors.append(f"{CONTROL_SOURCE}: incomplete unlabeled control semantics: {required}")

    body_build = verifier.find("BodyBindingBuilder::build(input, arena, skeleton)")
    control_build = verifier.find("ControlTransferBuilder::build(input, arena)")
    failure_merge = verifier.find("zc::TreeMap<PendingFailureOrderKey, PendingFailureRef>")
    if (
        body_build < 0
        or control_build < 0
        or failure_merge < 0
        or not body_build < control_build < failure_merge
    ):
        errors.append(
            f"{VERIFIER_SOURCE}: control transfer must run after body binding and before "
            "failure merge"
        )
    for required in (
        "enum class PendingFailureKind : uint8_t { Duplicate, BodyLookup, LabelDuplicate, ControlTransfer };",
        "controlResult.is<ControlTransferCandidate>()",
        "control.failures.size()",
        "PendingFailureRef{PendingFailureKind::ControlTransfer, index}",
        "static_cast<uint8_t>(BinderEmitterSite::BodyBinding)",
        "ordered.value.kind == PendingFailureKind::ControlTransfer",
        "BindingDiagnosticAdapter::emitControlTransferFailure(",
        "BindingResolutionValue(FailedBindingResolution{failureIndex})",
        "candidate.controlTransfers = zc::mv(control.controlTransfers)",
    ):
        if required not in verifier:
            errors.append(f"{VERIFIER_SOURCE}: control failure merge is disconnected: {required}")
    for required in (
        "static_cast<uint8_t>(BinderEmitterSite::BodyBinding),\n"
        "                               controlFailure.schemaPreorderOrdinal, sequence++}",
        "controlFailure.diagnostic,\n"
        "                tree.node(controlFailure.node).range.getStart()",
        "BindingFailureRef{controlFailure.diagnostic, controlFailure.source.clone(),\n"
        "                                           emitterOrdinal, zc::mv(noNotes)}",
        "controlFailure.node, BindingResolutionValue(FailedBindingResolution{failureIndex})",
    ):
        if required not in verifier:
            errors.append(
                f"{VERIFIER_SOURCE}: control failure provenance merge is incomplete: {required}"
            )

    target_codec = function_body(verifier, "bool encodeControlTarget(")
    for required in (
        "target.is<LoopControlTarget>()",
        "encoder.encodeUint8(0x02);",
        "target.get<LoopControlTarget>().scope",
        "target.is<MatchControlTarget>()",
        "encoder.encodeUint8(0x03);",
        "target.get<MatchControlTarget>().scope",
    ):
        if required not in target_codec:
            errors.append(f"{VERIFIER_SOURCE}: incomplete control target codec: {required}")
    control_fact_codec = (
        "encoder.encodeSequenceSize(candidate.controlTransfers.size());\n"
        "  for (const auto& fact : candidate.controlTransfers) {\n"
        "    encoder.encodeUint32(fact.node.value);\n"
        "    encoder.encodeUint8(static_cast<uint8_t>(fact.kind));\n"
        "    if (!encodeControlTarget(encoder, input, fact.target)) { return zc::none; }\n"
        "    fact.source.encode(encoder);\n"
        "  }"
    )
    if control_fact_codec not in verifier:
        errors.append(
            f"{VERIFIER_SOURCE}: control fact codec must cover node, kind, target, and source"
        )

    foreign_check = function_body(verifier, "bool hasForeignContext(")
    for required in (
        "for (const auto& fact : candidate.controlTransfers)",
        "const auto& target = fact.target;",
        "target.is<LoopControlTarget>()",
        "target.get<LoopControlTarget>().scope",
        "target.is<MatchControlTarget>()",
        "target.get<MatchControlTarget>().scope",
        "scope.module() != input.module() || !scope.belongsTo(input.semanticContext())",
    ):
        if required not in foreign_check:
            errors.append(
                f"{VERIFIER_SOURCE}: control foreign-context check is incomplete: {required}"
            )
    source_check = function_body(verifier, "bool hasInvalidSourceRange(")
    for required in (
        "for (const auto& fact : candidate.controlTransfers)",
        "if (spanIsInvalid(fact.source)) { return true; }",
    ):
        if required not in source_check:
            errors.append(
                f"{VERIFIER_SOURCE}: control source-range check is incomplete: {required}"
            )
    for required in (
        "candidate.controlTransfers.size() < expected.controlTransfers.size()",
        "candidate.controlTransfers.size() > expected.controlTransfers.size()",
    ):
        if required not in verifier:
            errors.append(f"{VERIFIER_SOURCE}: control fact size check is incomplete: {required}")

    oracle = function_body(verifier, "ControlOracleResult verifyControlTransferFacts(")
    if not oracle:
        errors.append(f"{VERIFIER_SOURCE}: independent control-transfer oracle is missing")
    else:
        if "ControlTransferBuilder::build" in oracle:
            errors.append(f"{VERIFIER_SOURCE}: control-transfer oracle reuses builder authority")
        for required in (
            "ScopeArenaBuilder::build(input)",
            "ast::visitTreePreOrder(tree, tree.root()",
            "scope.kind == ScopeKind::Loop",
            "scope.kind == ScopeKind::Match && isBreak",
            "scope.kind == ScopeKind::Function || scope.kind == ScopeKind::Closure ||",
            "fact.kind != expectedKind",
            "!sameSpan(fact.source, source)",
            "fact.target.is<LoopControlTarget>()",
            "fact.target.is<MatchControlTarget>()",
            "resolutionIndex != kMissing",
            "factIndex != kMissing",
            "resolutionIndex == kMissing",
            "resolution.value.is<FailedBindingResolution>()",
            "failureFact.diagnostic != expectedDiagnostic",
            "retainedTokenSpan(",
            "node, 0,",
            "BinderEmitterSite::BodyBinding",
            "schemaOrdinal != schemaOrdinals[node.value]",
            "localOrdinal != 0",
            "consumedFacts",
            "consumedFailures",
        ):
            if required not in oracle:
                errors.append(
                    f"{VERIFIER_SOURCE}: incomplete independent control-transfer oracle: {required}"
                )

    census = function_body(verifier, "bool hasCompleteLexicalBindingSites(")
    for required in (
        "requiredNamespaces[binding.node.value] == 0",
        "nodeKind != ast::SyntaxKind::BreakStmt",
        "nodeKind != ast::SyntaxKind::ContinueStatement",
        "!binding.value.is<FailedBindingResolution>()",
        "published[binding.node.value] = true;",
        "continue;",
    ):
        if required not in census:
            errors.append(
                f"{VERIFIER_SOURCE}: control failure XOR census is incomplete: {required}"
            )

    if (
        "emitControlTransferFailure(" not in adapter_header
        or "emitControlTransferFailure(" not in adapter_source
    ):
        errors.append("binding diagnostic adapter lacks control projection")
    for required in (
        "BinderDiagnosticCode::BreakTargetNotFound",
        "BinderDiagnosticCode::ContinueTargetNotFound",
    ):
        if required not in adapter_source:
            errors.append(f"{DIAGNOSTIC_ADAPTER_SOURCE}: missing control projection: {required}")
    for required in (
        'DIAG(3020, BreakTargetNotFound, kError,\n'
        '     "break requires an enclosing loop, match, or label", 0)',
        'DIAG(3021, ContinueTargetNotFound, kError,\n'
        '     "continue requires an enclosing loop or loop label", 0)',
    ):
        if required not in definitions:
            errors.append(
                f"{DIAGNOSTIC_DEFINITIONS}: missing stable control diagnostic: {required}"
            )

    for marker in (
        "ControlTransfer.TargetsEveryLoopForm",
        "ControlTransfer.BreakTargetsMatchWhileContinueSkipsMatch",
        "ControlTransfer.SelectsInnerLoopInsideMatch",
        "ControlTransfer.StopsAtFunctionAndClosureBoundaries",
        "ControlTransfer.OrdersExactKeywordFailures",
        "ControlTransfer.FailsClosedForExplicitLabels",
        "BindingVerifier.RejectsMissingAdditionalAndReorderedControlTransfers",
        "BindingVerifier.RejectsInvalidControlTargetsAndSources",
        "BindingVerifier.RejectsForeignControlTargets",
        "BindingVerifier.EnforcesControlFailureXor",
    ):
        if marker not in tests:
            errors.append(f"{TEST_SOURCE}: missing control-transfer evidence: {marker}")
    for marker in (
        "BindingDiagnosticAdapter.EmitsZeroArgumentControlTransferFailures",
        "BindingDiagnosticAdapter.RejectsUnsupportedControlTransferCodes",
    ):
        if marker not in files.get(DIAGNOSTIC_ADAPTER_TEST, ""):
            errors.append(f"{DIAGNOSTIC_ADAPTER_TEST}: missing control adapter evidence: {marker}")

    internal_include = '"zomlang/compiler/binder/internal/control-transfer.h"'
    for path, text in files.items():
        if "ContinueTargetNotLoop" in text:
            errors.append(f"{path}: obsolete ContinueTargetNotLoop contract is forbidden")
        if path in {CONTROL_HEADER, CONTROL_SOURCE, VERIFIER_SOURCE} or TEST_DIR in path.parents:
            continue
        if internal_include in text or re.search(r"\bControlTransferBuilder::build\(", text):
            errors.append(f"{path}: control-transfer internal authority escaped")


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
            if re.search(rf"\b{re.escape(symbol)}", text):
                errors.append(f"{path}: binder-internal authority escaped through {symbol}")


def check_scope_arena_contract(files: dict[Path, str], errors: list[str]) -> None:
    header = files.get(SCOPE_HEADER, "")
    source = files.get(SCOPE_SOURCE, "")
    internal_include = '"zomlang/compiler/binder/internal/scope-arena.h"'
    for path, text in files.items():
        if path in {SCOPE_HEADER, SCOPE_SOURCE, SKELETON_HEADER, SKELETON_SOURCE,
                    BODY_HEADER, BODY_SOURCE, LABEL_HEADER, LABEL_SOURCE, CONTROL_HEADER, CONTROL_SOURCE,
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
        if path in {SKELETON_HEADER, SKELETON_SOURCE, BODY_SOURCE,
                    VERIFIER_SOURCE} or TEST_DIR in path.parents:
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
        "struct BindingDuplicateFact final",
        "BinderEmitterSite emitterSite;",
        "zc::Vector<DefinitionFact> definitions;",
        "zc::Vector<ImplBindingFact> impls;",
        "zc::Vector<BindingDuplicateFact> duplicates;",
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
        "BindingBuilder.PublishesUndefinedIdentifierFailure",
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
        "emitLookupFailure(",
        "VerifiedIdentifierArgument&& identifier",
    ):
        if required not in header:
            errors.append(f"{DIAGNOSTIC_ADAPTER_HEADER}: incomplete typed adapter: {required}")
    for required in (
        "VerifiedIdentifierArgument::from(",
        "case BinderDiagnosticCode::UndefinedIdentifier:",
        "case BinderDiagnosticCode::SymbolNamespaceMismatch:",
        "diagnostics::DiagID::PreviousDeclarationHere",
        "case BinderDiagnosticCode::DuplicateIdentifier:",
    ):
        if required not in source:
            errors.append(f"{DIAGNOSTIC_ADAPTER_SOURCE}: incomplete redeclaration adapter: {required}")
    if 'DIAG(3017, PreviousDeclarationHere, kNote, "Previous declaration is here", 0)' not in definitions:
        errors.append(f"{DIAGNOSTIC_DEFINITIONS}: missing ZOM3017 previous declaration note")
    if "BindingDiagnosticAdapter.EmitsTypedRedeclarationWithPreviousNote" not in files.get(TEST_SOURCE, ""):
        errors.append(f"{TEST_SOURCE}: missing typed redeclaration adapter evidence")
    if "BindingBuilder.PublishesUndefinedIdentifierFailure" not in files.get(TEST_SOURCE, ""):
        errors.append(f"{TEST_SOURCE}: missing typed lookup failure evidence")
    if "engine.diagnose<" in files.get(VERIFIER_SOURCE, ""):
        errors.append(f"{VERIFIER_SOURCE}: raw diagnostics bypass the typed binding adapter")


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
        (BINDER_CMAKE, "${CMAKE_CURRENT_SOURCE_DIR}/body-binding.cc"),
        (BINDER_CMAKE, "${CMAKE_CURRENT_SOURCE_DIR}/label-facts.cc"),
        (BINDER_CMAKE, "${CMAKE_CURRENT_SOURCE_DIR}/control-transfer.cc"),
        (BINDER_CMAKE, "${CMAKE_CURRENT_SOURCE_DIR}/binding-diagnostic-adapter.cc"),
        (TEST_CMAKE, 'add_ztest_unit_test("binding-input-test" "binding-input-test.cc"'),
        (TEST_CMAKE,
         'add_ztest_unit_test("binding-diagnostic-adapter-test" '
         '"binding-diagnostic-adapter-test.cc"'),
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
        "encodeAllocationLabelRecord(",
        "encodeBindingAllocationDump(",
        "encodeBindingAllocationDump(input, candidate.scopes.asPtr(), candidate.labels.asPtr())",
        "encodeBindingAllocationDump(input, expected.scopes.asPtr(), expected.labels.asPtr())",
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
    check_parsed_token_provenance(files, errors)
    check_unique_construction(files, errors)
    check_verified_input_surface(files, errors)
    check_frozen_impl_inventory_contract(files, errors)
    check_owned_key_projection_contract(files, errors)
    check_special_callable_contract(files, errors)
    check_closure_activation_contract(files, errors)
    check_pattern_activation_contract(files, errors)
    check_body_binding_contract(files, errors)
    check_label_fact_contract(files, errors)
    check_control_transfer_contract(files, errors)
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
    baseline = check(files)
    if baseline:
        return [f"self-test baseline rejected: {error}" for error in baseline]
    cases: tuple[tuple[str, Path, str, str], ...] = (
        ("public constructor", HEADER, "class VerifiedBindingInput final {\npublic:", "class VerifiedBindingInput final {\npublic:\n  explicit VerifiedBindingInput(int);"),
        (
            "resettable token stream authority",
            TOKEN_SNAPSHOT_HEADER,
            "friend class Parser;",
            "friend class Parser;\n  friend class TokenStream;",
        ),
        (
            "missing parsed token admission",
            PARSED_HEADER,
            "parser::ParsedTokenSnapshot&& tokens, ast::Tree&& tree",
            "ast::Tree&& tree",
        ),
        (
            "missing token kind verification",
            PARSED_SOURCE,
            "token.kind != expectedKind",
            "false",
        ),
        (
            "missing parser one-shot evidence",
            PARSER_TEST_SOURCE,
            "ParserTest.TokenSnapshotIsSingleUseAfterSuccessfulParse",
            "ParserTest.TokenSnapshotCanBeReused",
        ),
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
            "retained frozen registry lifetime",
            INVENTORY_SOURCE,
            "identity::DefinitionRegistry::FrozenKeyIndex definitionKeys;",
            "const identity::SemanticIdentityRegistrySet& registries;",
        ),
        (
            "linear frozen key projection",
            FROZEN_REGISTRY_HEADER,
            "return keys[handle.slot];",
            "for (const auto& key : keys) { return key; }",
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
            "ParsedModuleVerifier::admit(snapshot, sources, buffer, zc::mv(tokens), tree);",
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
            "missing body binding wiring",
            BINDER_CMAKE,
            "${CMAKE_CURRENT_SOURCE_DIR}/body-binding.cc",
            "${CMAKE_CURRENT_SOURCE_DIR}/missing-body-binding.cc",
        ),
        (
            "public label owner construction",
            METADATA_HEADER,
            "private:\n  explicit LabelOwner(LabelOwnerValue&& value) noexcept;",
            "public:\n  explicit LabelOwner(LabelOwnerValue&& value) noexcept;",
        ),
        (
            "public label identity construction",
            METADATA_HEADER,
            "private:\n  LabelId(LabelOwner&& owner, uint32_t index) noexcept;",
            "public:\n  LabelId(LabelOwner&& owner, uint32_t index) noexcept;",
        ),
        (
            "public label target construction",
            METADATA_HEADER,
            "private:\n  explicit LabelTarget(LabelTargetValue&& value) noexcept;",
            "public:\n  explicit LabelTarget(LabelTargetValue&& value) noexcept;",
        ),
        (
            "shared label construction authority",
            METADATA_HEADER,
            "LabelOwnerValue valueValue;\n  friend class LabelBuilder;",
            "LabelOwnerValue valueValue;\n  friend class BindingBuilder;",
        ),
        (
            "missing label facts wiring",
            BINDER_CMAKE,
            "${CMAKE_CURRENT_SOURCE_DIR}/label-facts.cc",
            "${CMAKE_CURRENT_SOURCE_DIR}/missing-label-facts.cc",
        ),
        (
            "foreign label facts include",
            Path("products/zomlang/compiler/checker/escape.cc"),
            "",
            '#include "zomlang/compiler/binder/internal/label-facts.h"',
        ),
        (
            "non-preorder label allocation",
            LABEL_SOURCE,
            "ast::visitTreePreOrder(tree, tree.root()",
            "ast::visitChildNodeIds(tree, tree.root()",
        ),
        (
            "global label allocation counter",
            LABEL_SOURCE,
            "counters[index].owner == ownerValue",
            "index == 0",
        ),
        (
            "missing closure label owner",
            LABEL_SOURCE,
            "scope.kind == ScopeKind::Function || scope.kind == ScopeKind::Closure",
            "scope.kind == ScopeKind::Function",
        ),
        (
            "missing nested label flattening",
            LABEL_SOURCE,
            "syntax.kind != ast::SyntaxKind::LabeledStatement",
            "syntax.kind != ast::SyntaxKind::EmptyStatement",
        ),
        (
            "missing for-in label target",
            LABEL_SOURCE,
            "kind == ast::SyntaxKind::ForInStatement",
            "kind == ast::SyntaxKind::MatchStatement",
        ),
        (
            "wrong label token ordinal",
            LABEL_SOURCE,
            "retainedTokenSpan(node, 0, ast::SyntaxKind::Identifier)",
            "retainedTokenSpan(node, 1, ast::SyntaxKind::Identifier)",
        ),
        (
            "non-canonical label names",
            LABEL_SOURCE,
            "identity::SemanticIdentifier::fromSource(",
            "identity::SemanticIdentifier::fromCanonical(",
        ),
        (
            "cross-owner label duplicates",
            LABEL_SOURCE,
            "prior.owner == ownerValue && prior.name == nameValue",
            "prior.name == nameValue",
        ),
        (
            "disconnected label builder cutover",
            VERIFIER_SOURCE,
            "LabelBuilder::build(input, arena)",
            "disconnectedLabelBuilder(input, arena)",
        ),
        (
            "missing label candidate publication",
            VERIFIER_SOURCE,
            "candidate.labels = zc::mv(labels.labels)",
            "candidate.labels = zc::Vector<LabelFact>()",
        ),
        (
            "missing label statement codec",
            VERIFIER_SOURCE,
            "encoder.encodeUint32(fact.statement.value);",
            "encoder.encodeUint32(0);",
        ),
        (
            "missing allocation label records",
            VERIFIER_SOURCE,
            "encodeAllocationLabelRecord(input, scopes, fact)",
            "missingAllocationLabelRecord(input, scopes, fact)",
        ),
        (
            "missing label foreign-context check",
            VERIFIER_SOURCE,
            "for (const auto& fact : candidate.labels) {\n"
            "    if (!fact.owner.belongsTo(input.semanticContext())",
            "for (const auto& fact : candidate.controlTransfers) {\n"
            "    if (!fact.owner.belongsTo(input.semanticContext())",
        ),
        (
            "missing label source-range check",
            VERIFIER_SOURCE,
            "for (const auto& fact : candidate.labels) {\n"
            "    if (spanIsInvalid(fact.source)) { return true; }\n"
            "  }",
            "",
        ),
        (
            "missing smaller label fact classification",
            VERIFIER_SOURCE,
            "candidate.labels.size() < expected.labels.size()",
            "false",
        ),
        (
            "missing larger label fact classification",
            VERIFIER_SOURCE,
            "candidate.labels.size() > expected.labels.size()",
            "false",
        ),
        (
            "label oracle reuses builder",
            VERIFIER_SOURCE,
            "ScopeArenaBuilder::build(input)",
            "LabelBuilder::build(input, arena)",
        ),
        (
            "label oracle drops exact declaration token",
            VERIFIER_SOURCE,
            "retainedTokenSpan(node, 0, ast::SyntaxKind::Identifier)",
            "spanFor(tree.node(node).range)",
        ),
        (
            "disconnected candidate label oracle",
            VERIFIER_SOURCE,
            "const auto candidateLabels = verifyLabelFacts(input, candidate);",
            "const auto candidateLabels = LabelOracleResult::Valid;",
        ),
        (
            "restored ast label target side table",
            AST_TREE_HEADER,
            "",
            "\nvoid setLabelTarget(ast::NodeId node, ast::NodeId target);\n",
        ),
        (
            "explicit label references accepted prematurely",
            CONTROL_SOURCE,
            "if (label != 0)",
            "if (label == 0)",
        ),
        (
            "premature bound label resolution",
            METADATA_HEADER,
            "",
            "\nstruct BoundLabel final {};\n",
        ),
        (
            "premature ZOM3022 registration",
            DIAGNOSTIC_DEFINITIONS,
            "",
            '\nDIAG(3022, ContinueTargetNotLoop, kError, "continue label failed", 0)\n',
        ),
        (
            "missing label behavior evidence",
            TEST_SOURCE,
            "LabelFacts.ResolvesAllTargetsNestedLabelsAndPreservesControlTransfers",
            "LabelFacts.MissingNestedTargetEvidence",
        ),
        (
            "missing control transfer wiring",
            BINDER_CMAKE,
            "${CMAKE_CURRENT_SOURCE_DIR}/control-transfer.cc",
            "${CMAKE_CURRENT_SOURCE_DIR}/missing-control-transfer.cc",
        ),
        (
            "missing control scope dependency",
            CONTROL_HEADER,
            '#include "zomlang/compiler/binder/internal/scope-arena.h"',
            '#include "zomlang/compiler/binder/binding-metadata.h"',
        ),
        (
            "foreign control transfer include",
            Path("products/zomlang/compiler/checker/escape.cc"),
            "",
            '#include "zomlang/compiler/binder/internal/control-transfer.h"',
        ),
        (
            "control continue targets match",
            CONTROL_SOURCE,
            "scope.kind == ScopeKind::Match && isBreak",
            "scope.kind == ScopeKind::Match",
        ),
        (
            "missing control callable boundary",
            CONTROL_SOURCE,
            "scope.kind == ScopeKind::Function || scope.kind == ScopeKind::Closure ||",
            "scope.kind == ScopeKind::Function ||",
        ),
        (
            "missing exact control keyword provenance",
            CONTROL_SOURCE,
            "input.parsedModule().retainedTokenSpan(",
            "input.parsedModule().spanFor(",
        ),
        (
            "restored non-empty label rejection",
            CONTROL_SOURCE,
            "if (syntax.kind != ast::SyntaxKind::BreakStmt &&",
            "if (syntax.kind == ast::SyntaxKind::LabeledStatement) { return; }\n"
            "      if (syntax.kind != ast::SyntaxKind::BreakStmt &&",
        ),
        (
            "disconnected control transfer cutover",
            VERIFIER_SOURCE,
            "ControlTransferBuilder::build(input, arena)",
            "disconnectedControlTransfer(input, arena)",
        ),
        (
            "missing control failure kind",
            VERIFIER_SOURCE,
            "enum class PendingFailureKind : uint8_t { Duplicate, BodyLookup, LabelDuplicate, ControlTransfer };",
            "enum class PendingFailureKind : uint8_t { Duplicate, BodyLookup, LabelDuplicate };",
        ),
        (
            "disconnected control diagnostic merge",
            VERIFIER_SOURCE,
            "BindingDiagnosticAdapter::emitControlTransferFailure(",
            "disconnectedControlTransferFailure(",
        ),
        (
            "wrong control failure emitter site",
            VERIFIER_SOURCE,
            "static_cast<uint8_t>(BinderEmitterSite::BodyBinding),\n"
            "                               controlFailure.schemaPreorderOrdinal, sequence++}",
            "static_cast<uint8_t>(BinderEmitterSite::LabelAndClosure),\n"
            "                               controlFailure.schemaPreorderOrdinal, sequence++}",
        ),
        (
            "missing control failed resolution",
            VERIFIER_SOURCE,
            "controlFailure.node, BindingResolutionValue(FailedBindingResolution{failureIndex})",
            "controlFailure.node, BindingResolutionValue(UnresolvedBindingResolution{})",
        ),
        (
            "wrong loop control target tag",
            VERIFIER_SOURCE,
            "encoder.encodeUint8(0x02);",
            "encoder.encodeUint8(0x04);",
        ),
        (
            "missing control fact node codec",
            VERIFIER_SOURCE,
            "encoder.encodeSequenceSize(candidate.controlTransfers.size());\n"
            "  for (const auto& fact : candidate.controlTransfers) {\n"
            "    encoder.encodeUint32(fact.node.value);",
            "encoder.encodeSequenceSize(candidate.controlTransfers.size());\n"
            "  for (const auto& fact : candidate.controlTransfers) {\n"
            "    encoder.encodeUint32(0);",
        ),
        (
            "missing control fact kind codec",
            VERIFIER_SOURCE,
            "encoder.encodeUint32(fact.node.value);\n"
            "    encoder.encodeUint8(static_cast<uint8_t>(fact.kind));\n"
            "    if (!encodeControlTarget(encoder, input, fact.target))",
            "encoder.encodeUint32(fact.node.value);\n"
            "    encoder.encodeUint8(0);\n"
            "    if (!encodeControlTarget(encoder, input, fact.target))",
        ),
        (
            "missing control fact target codec",
            VERIFIER_SOURCE,
            "if (!encodeControlTarget(encoder, input, fact.target)) { return zc::none; }",
            "if (false) { return zc::none; }",
        ),
        (
            "missing control fact source codec",
            VERIFIER_SOURCE,
            "if (!encodeControlTarget(encoder, input, fact.target)) { return zc::none; }\n"
            "    fact.source.encode(encoder);",
            "if (!encodeControlTarget(encoder, input, fact.target)) { return zc::none; }",
        ),
        (
            "missing control foreign context check",
            VERIFIER_SOURCE,
            "for (const auto& fact : candidate.controlTransfers) {\n"
            "    const auto& target = fact.target;",
            "for (const auto& fact : candidate.controlTransfers) {\n"
            "    const auto& target = candidate.controlTransfers[0].target;",
        ),
        (
            "missing control source range check",
            VERIFIER_SOURCE,
            "for (const auto& fact : candidate.controlTransfers) {\n"
            "    if (spanIsInvalid(fact.source)) { return true; }\n"
            "  }",
            "",
        ),
        (
            "missing smaller control fact classification",
            VERIFIER_SOURCE,
            "candidate.controlTransfers.size() < expected.controlTransfers.size()",
            "false",
        ),
        (
            "missing larger control fact classification",
            VERIFIER_SOURCE,
            "candidate.controlTransfers.size() > expected.controlTransfers.size()",
            "false",
        ),
        (
            "control oracle reuses builder",
            VERIFIER_SOURCE,
            "ScopeArenaBuilder::build(input)",
            "ControlTransferBuilder::build(input, arena)",
        ),
        (
            "control oracle permits continue to match",
            VERIFIER_SOURCE,
            "scope.kind == ScopeKind::Match && isBreak",
            "scope.kind == ScopeKind::Match",
        ),
        (
            "control oracle drops callable boundary",
            VERIFIER_SOURCE,
            "scope.kind == ScopeKind::Function || scope.kind == ScopeKind::Closure ||",
            "scope.kind == ScopeKind::Function ||",
        ),
        (
            "control oracle drops exact failure primary",
            VERIFIER_SOURCE,
            "auto expectedPrimary = input.parsedModule().retainedTokenSpan(",
            "auto expectedPrimary = input.parsedModule().spanFor(",
        ),
        (
            "missing control failure XOR census",
            VERIFIER_SOURCE,
            "nodeKind != ast::SyntaxKind::ContinueStatement",
            "nodeKind != ast::SyntaxKind::BreakStmt",
        ),
        (
            "missing control behavior evidence",
            TEST_SOURCE,
            "ControlTransfer.BreakTargetsMatchWhileContinueSkipsMatch",
            "ControlTransfer.MissingMatchPartitionEvidence",
        ),
        (
            "missing zero argument control adapter evidence",
            DIAGNOSTIC_ADAPTER_TEST,
            "BindingDiagnosticAdapter.EmitsZeroArgumentControlTransferFailures",
            "BindingDiagnosticAdapter.MissingZeroArgumentControlTransferFailures",
        ),
        (
            "unstable control diagnostic registration",
            DIAGNOSTIC_DEFINITIONS,
            'DIAG(3020, BreakTargetNotFound, kError,\n'
            '     "break requires an enclosing loop, match, or label", 0)',
            'DIAG(3020, BreakTargetNotFound, kError, "break failed", 0)',
        ),
        (
            "obsolete continue diagnostic contract",
            METADATA_HEADER,
            "",
            "\nenum class EscapedControlDiagnostic { ContinueTargetNotLoop };\n",
        ),
        (
            "disconnected body binding cutover",
            VERIFIER_SOURCE,
            "BodyBindingBuilder::build(input, arena, skeleton)",
            "disconnectedBodyBinding(input, arena, skeleton)",
        ),
        (
            "premature local activation",
            BODY_SOURCE,
            "if (tree.contains(initializer)) { visitNode(initializer, scopeIndex, Namespace::Value); }",
            "activateIntroducer(node, DefinitionActivation::AfterInitializer, true);\n"
            "    if (tree.contains(initializer)) { visitNode(initializer, scopeIndex, Namespace::Value); }",
        ),
        (
            "restored local skeleton activation",
            SKELETON_SOURCE,
            "if (definition.kind == identity::DefinitionKind::Local) { continue; }",
            "if (definition.kind == identity::DefinitionKind::Local) {}",
        ),
        (
            "missing body emitter provenance",
            SKELETON_HEADER,
            "BinderEmitterSite emitterSite;",
            "BinderEmitterSite missingEmitterSite;",
        ),
        (
            "missing earlier declarator evidence",
            TEST_SOURCE,
            "BodyBinding.ResolvesEarlierDeclaratorsAfterActivation",
            "BodyBinding.MissingEarlierDeclaratorsAfterActivation",
        ),
        (
            "missing self reference evidence",
            TEST_SOURCE,
            "BodyBinding.RejectsSelfReferenceBeforeActivation",
            "BodyBinding.MissingSelfReferenceBeforeActivation",
        ),
        (
            "missing type path resolution",
            BODY_SOURCE,
            "resolveIdentifierPath(node, scopeIndex, inherited)",
            "skipIdentifierPath(node, scopeIndex, inherited)",
        ),
        (
            "missing type query value routing",
            BODY_SOURCE,
            "visitTypeQuery(node, scopeIndex)",
            "visitSchemaChildren(node, scopeIndex, inherited)",
        ),
        (
            "missing dyn marker routing",
            BODY_SOURCE,
            "visitDynTypeMarkers(node, scopeIndex)",
            "visitSchemaChildren(node, scopeIndex, inherited)",
        ),
        (
            "missing object shorthand routing",
            BODY_SOURCE,
            "visitObjectProperty(node, scopeIndex)",
            "visitSchemaChildren(node, scopeIndex, inherited)",
        ),
        (
            "missing type query census role",
            VERIFIER_SOURCE,
            "case ast::SyntaxKind::TypeQueryExpr:",
            "case ast::SyntaxKind::TypeOfExpression:",
        ),
        (
            "missing dyn marker census role",
            VERIFIER_SOURCE,
            "case ast::SyntaxKind::DynTypeMarkerList:",
            "case ast::SyntaxKind::DynTypeIfaceList:",
        ),
        (
            "missing object shorthand census role",
            VERIFIER_SOURCE,
            "case ast::SyntaxKind::ObjectProperty:",
            "case ast::SyntaxKind::ObjectSpread:",
        ),
        (
            "missing optional struct pattern path guard",
            VERIFIER_SOURCE,
            "if (tree.contains(typePath)) {\n          requireSite(typePath, ast::SyntaxKind::ModulePath, Namespace::Type);",
            "requireSite(typePath, ast::SyntaxKind::ModulePath, Namespace::Type);",
        ),
        (
            "premature callable parameter activation",
            BODY_SOURCE,
            "visitParameterSignature(parameter, scopeIndex);",
            "visitParameterDefaultAndActivate(parameter, scopeIndex);",
        ),
        (
            "missing lexical binding census",
            VERIFIER_SOURCE,
            "hasCompleteLexicalBindingSites(input.tree(), expected.nodeBindings.asPtr())",
            "expected.nodeBindings.empty()",
        ),
        (
            "missing constant time definition key lookup",
            VERIFIER_SOURCE,
            "input.definitions().definitionKey(definition)",
            "missingDefinitionKey(definition)",
        ),
        (
            "missing callable signature evidence",
            TEST_SOURCE,
            "BodyBinding.KeepsParametersOutOfReturnTypes",
            "BodyBinding.MissingParametersOutOfReturnTypes",
        ),
        (
            "foreign body binding include",
            Path("products/zomlang/compiler/checker/escape.cc"),
            "",
            '#include "zomlang/compiler/binder/internal/body-binding.h"',
        ),
        (
            "missing bound name codec",
            VERIFIER_SOURCE,
            "!encodeTarget(encoder, input, bound.canonicalTarget)",
            "!encodeTarget(encoder, input, bound.bindingIdentity)",
        ),
        (
            "missing shadow target codec",
            VERIFIER_SOURCE,
            "encodeSequenceSize(candidate.shadowTargets.size())",
            "encodeSequenceSize(0)",
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
            "encodeBindingAllocationDump(input, candidate.scopes.asPtr(), candidate.labels.asPtr())",
            "disconnectedBindingAllocationDump(input, candidate.scopes.asPtr(), "
            "candidate.labels.asPtr())",
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
