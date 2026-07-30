#!/usr/bin/env python3
"""Validate the hand-authored stable Binder schema and its mutation defenses."""

from __future__ import annotations

import argparse
import ast
import re
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SCHEMA = ROOT / "products/zomlang/compiler/binder/stable-binding-schema.def"
FACTS_HEADER = ROOT / "products/zomlang/compiler/binder/stable-binding-facts.h"
FACTS_SOURCE = ROOT / "products/zomlang/compiler/binder/stable-binding-facts.cc"
CODEC_HEADER = ROOT / "products/zomlang/compiler/binder/stable-binding-codec.h"
CODEC_SOURCE = ROOT / "products/zomlang/compiler/binder/stable-binding-codec.cc"
BINDER_CMAKE = ROOT / "products/zomlang/compiler/binder/CMakeLists.txt"
CONTEXTUAL_HEADER = ROOT / "products/zomlang/compiler/driver/contextual-binding-key.h"
CONTEXTUAL_SOURCE = ROOT / "products/zomlang/compiler/driver/contextual-binding-key.cc"
METADATA_HEADER = ROOT / "products/zomlang/compiler/binder/binding-metadata.h"
NATIVE_TEST = (
    ROOT
    / "products/zomlang/tests/unittests/compiler/binder/stable-binding-facts-test.cc"
)
TEST_CMAKE = (
    ROOT / "products/zomlang/tests/unittests/compiler/binder/CMakeLists.txt"
)
CONTEXTUAL_TEST = (
    ROOT
    / "products/zomlang/tests/unittests/compiler/driver/"
    "active-definition-authority-query-test.cc"
)

REPOSITORY_INPUTS = {
    "facts-header": FACTS_HEADER,
    "facts-source": FACTS_SOURCE,
    "codec-header": CODEC_HEADER,
    "codec-source": CODEC_SOURCE,
    "binder-cmake": BINDER_CMAKE,
    "contextual-header": CONTEXTUAL_HEADER,
    "contextual-source": CONTEXTUAL_SOURCE,
    "metadata-header": METADATA_HEADER,
    "native-test": NATIVE_TEST,
    "test-cmake": TEST_CMAKE,
    "contextual-test": CONTEXTUAL_TEST,
}

KINDS = {
    "BOUND": "Bound",
    "RECORD": "Record",
    "NESTED_RECORD": "NestedRecord",
    "NESTED_FIELD": "NestedField",
    "SUM": "Sum",
    "RUNTIME_SUM": "RuntimeSum",
    "ENUM_VALUE": "EnumValue",
    "SUM_VARIANT": "SumVariant",
    "VARIANT_FIELD": "VariantField",
    "INLINE_SUM_VARIANT": "InlineSumVariant",
    "INLINE_SUM_VARIANT_FIELD": "InlineSumVariantField",
    "RUNTIME_SUM_VARIANT": "RuntimeSumVariant",
    "RUNTIME_VARIANT_FIELD": "RuntimeVariantField",
    "FIELD": "Field",
    "FIELD_LIMIT": "FieldLimit",
    "QUERY": "Query",
    "INPUT": "Input",
    "CAPABILITY_QUERY": "CapabilityQuery",
    "MATERIALIZER_PERMISSION": "MaterializerPermission",
    "DIAGNOSTIC_MAPPING": "DiagnosticMapping",
    "CONSTRAINT": "Constraint",
    "DIGEST": "Digest",
}
ARITIES = {
    "Bound": 3,
    "Record": 10,
    "NestedRecord": 9,
    "NestedField": 4,
    "Sum": 6,
    "RuntimeSum": 5,
    "EnumValue": 8,
    "SumVariant": 3,
    "VariantField": 5,
    "InlineSumVariant": 5,
    "InlineSumVariantField": 6,
    "RuntimeSumVariant": 4,
    "RuntimeVariantField": 5,
    "Field": 4,
    "FieldLimit": 2,
    "Query": 13,
    "Input": 11,
    "CapabilityQuery": 14,
    "MaterializerPermission": 3,
    "DiagnosticMapping": 11,
    "Constraint": 2,
    "Digest": 7,
}
MACRO_SIGNATURES = {
    "BOUND": ("name", "limit", "rule"),
    "RECORD": (
        "name",
        "domain",
        "maximum",
        "producer",
        "verifier",
        "typeTask",
        "codecTask",
        "testTask",
        "mutations",
        "test",
    ),
    "NESTED_RECORD": (
        "name",
        "typeParameter1",
        "typeParameter2",
        "maximum",
        "typeTask",
        "codecTask",
        "testTask",
        "mutations",
        "test",
    ),
    "NESTED_FIELD": ("recordName", "ordinal", "fieldName", "fieldType"),
    "SUM": ("name", "typeTask", "codecTask", "testTask", "mutations", "test"),
    "RUNTIME_SUM": ("name", "typeTask", "testTask", "mutations", "test"),
    "ENUM_VALUE": (
        "enumName",
        "valueName",
        "tag",
        "typeTask",
        "codecTask",
        "testTask",
        "mutations",
        "test",
    ),
    "SUM_VARIANT": ("sumName", "variantName", "tag"),
    "VARIANT_FIELD": ("sumName", "variantName", "ordinal", "fieldName", "fieldType"),
    "INLINE_SUM_VARIANT": (
        "recordName",
        "fieldOrdinal",
        "fieldName",
        "variantName",
        "tag",
    ),
    "INLINE_SUM_VARIANT_FIELD": (
        "recordName",
        "fieldName",
        "variantName",
        "ordinal",
        "variantFieldName",
        "variantFieldType",
    ),
    "RUNTIME_SUM_VARIANT": ("sumName", "variantName", "tag", "condition"),
    "RUNTIME_VARIANT_FIELD": (
        "sumName",
        "variantName",
        "ordinal",
        "fieldName",
        "fieldType",
    ),
    "FIELD": ("recordName", "ordinal", "fieldName", "..."),
    "FIELD_LIMIT": ("target", "bound"),
    "QUERY": (
        "name",
        "domain",
        "keyType",
        "resultType",
        "valueDomain",
        "producer",
        "verifier",
        "descriptorTask",
        "providerTask",
        "verifierTask",
        "testTask",
        "mutations",
        "test",
    ),
    "INPUT": (
        "name",
        "domain",
        "keyType",
        "resultType",
        "verifier",
        "descriptorTask",
        "providerTask",
        "verifierTask",
        "testTask",
        "mutations",
        "test",
    ),
    "CAPABILITY_QUERY": (
        "name",
        "domain",
        "keyType",
        "resultType",
        "capabilityType",
        "producer",
        "verifier",
        "failureAlternatives",
        "descriptorTask",
        "providerTask",
        "verifierTask",
        "testTask",
        "mutations",
        "test",
    ),
    "MATERIALIZER_PERMISSION": ("descriptor", "keyType", "membershipDescriptor"),
    "DIAGNOSTIC_MAPPING": (
        "name",
        "code",
        "arguments",
        "secondaryCode",
        "secondaryRole",
        "secondaryCount",
        "fixItCount",
        "diagnosticTask",
        "testTask",
        "mutations",
        "test",
    ),
    "CONSTRAINT": ("target", "rule"),
    "DIGEST": (
        "name",
        "domain",
        "preimage",
        "implementationTask",
        "testTask",
        "mutations",
        "test",
    ),
}
EXPECTED_COUNTS = {
    "Bound": 17,
    "Record": 94,
    "NestedRecord": 1,
    "NestedField": 2,
    "Sum": 13,
    "RuntimeSum": 1,
    "EnumValue": 31,
    "SumVariant": 51,
    "VariantField": 47,
    "InlineSumVariant": 2,
    "InlineSumVariantField": 3,
    "RuntimeSumVariant": 4,
    "RuntimeVariantField": 4,
    "Field": 320,
    "FieldLimit": 121,
    "Query": 22,
    "Input": 6,
    "CapabilityQuery": 5,
    "MaterializerPermission": 20,
    "DiagnosticMapping": 5,
    "Constraint": 24,
    "Digest": 1,
}
TASKS = {
    "S2A",
    "S2B",
    "S2C",
    "S2D",
    "S2E",
    "S3",
    "S6",
    "I1A",
    "I2",
    "B1",
    "B2",
    "B4",
    "M1",
    "M2",
    "M3",
    "M5",
    "Q3",
    "T1",
    "R30_13",
    "R29_13A",
    "R29_13C",
    "R28_16A",
    "R28_16B",
}
TASK_COLUMNS = {
    "Record": (5, 6, 7),
    "NestedRecord": (4, 5, 6),
    "Sum": (1, 2, 3),
    "RuntimeSum": (1, 2),
    "EnumValue": (3, 4, 5),
    "Query": (7, 8, 9, 10),
    "Input": (5, 6, 7, 8),
    "CapabilityQuery": (8, 9, 10, 11),
    "DiagnosticMapping": (7, 8),
    "Digest": (3, 4),
}
MUTATION_COLUMN = {
    "Record": 8,
    "NestedRecord": 7,
    "Sum": 4,
    "RuntimeSum": 3,
    "EnumValue": 6,
    "Query": 11,
    "Input": 9,
    "CapabilityQuery": 12,
    "DiagnosticMapping": 9,
    "Digest": 5,
}
TEST_COLUMN = {kind: ARITIES[kind] - 1 for kind in MUTATION_COLUMN}
KNOWN_MUTATIONS = {
    "additional",
    "alias",
    "allocation",
    "ancestry",
    "arguments",
    "authority",
    "body-local",
    "bool",
    "callable",
    "catalog",
    "child-failure",
    "code",
    "context",
    "coverage",
    "cycle",
    "dense",
    "dependency",
    "definition",
    "diagnostic-bijection",
    "diagnostics",
    "digest",
    "disjoint",
    "distribution",
    "domain",
    "duplicate",
    "edge",
    "enum",
    "equality",
    "export",
    "field",
    "fingerprint",
    "fix-it-count",
    "framing",
    "generic",
    "gap",
    "graph",
    "header",
    "identifier",
    "identity",
    "implementation",
    "inventory",
    "key",
    "lease",
    "lineage",
    "max-bytes",
    "max-count",
    "membership",
    "missing",
    "module",
    "name",
    "namespace",
    "occurrence",
    "options",
    "ordinal",
    "ordering",
    "overflow",
    "owner",
    "ownership",
    "path",
    "payload",
    "permission",
    "policy",
    "position",
    "precedence",
    "prelude",
    "presence",
    "profile",
    "provenance",
    "readiness",
    "receiver",
    "relation",
    "reordered",
    "request",
    "requester",
    "result",
    "revision",
    "roots",
    "scc",
    "seal",
    "search-roots",
    "secondary-code",
    "secondary-count",
    "secondary-role",
    "source",
    "tag",
    "target",
    "trailing",
    "transaction",
    "transaction-domain",
    "truncated",
    "value",
    "witness",
}

S2_RECORDS = {
    "S2A": {
        "StableDefinitionQueryKey",
        "StableImplementationQueryKey",
        "StableImplementationOccurrenceQueryKey",
        "StableGenericParameterQueryKey",
        "StableCallableParameterQueryKey",
        "StableSemanticImportQueryKey",
        "StableOwnerBodyQueryKey",
        "ContextualBodyOwnerKey",
        "ContextualCompilationUnitKey",
        "ContextualCrateKey",
        "ContextualSourceKey",
        "ContextualModuleKey",
        "ContextualDefinitionKey",
        "ContextualImplementationKey",
        "ContextualGenericParameterKey",
        "ContextualCallableParameterKey",
    },
    "S2B": {
        "StableHeaderGenericParameter",
        "StableHeaderCallableParameter",
        "StableDefinitionHeader",
        "StableImplementationOccurrenceHeader",
        "StableScopeOwnerKey",
        "StableBindingTargetKey",
        "StableNodeSyntaxRoot",
        "StableScopeFact",
        "StableNodeScopeFact",
        "StableDeclarationFact",
        "StableImplementationOccurrenceFact",
        "StableGenericParameterDeclarationFact",
        "StableCallableParameterDeclarationFact",
        "StableImportFact",
        "StableModuleAliasFact",
        "StableReexportStep",
        "StableLocalExportFact",
        "BoundModuleSkeleton",
        "BinderQueryResult<StableDefinitionHeader>",
        "BinderQueryResult<StableImplementationOccurrenceHeader>",
        "BinderQueryResult<BoundModuleSkeleton>",
        "BinderQueryResult<BoundOwnerBody>",
        "BinderQueryResult<ModuleBindingAllocationPlan>",
        "BinderKeyFailure",
    },
    "S2C": {
        "StableFailedLookupOutcome",
        "StableFailedLookupFact",
        "StableExportedBinding",
        "StableExportedBindingQueryKey",
        "StableScopeNameBucketQueryKey",
    },
    "S2D": {
        "StableBodyScopeFact",
        "StableBodyNodeScopeFact",
        "StableOwnerLocalBindingFact",
        "StableResolutionFact",
        "StableDeferredMemberFact",
        "StableSelfOwner",
        "StableSelfTypeFact",
        "StableThisBindingFact",
        "StableShadowTargetFact",
        "StableLabelKey",
        "StableLabelTarget",
        "StableLabelFact",
        "StableControlTarget",
        "StableControlTransferFact",
        "StableClosureFact",
        "StableClosureFreeVariable",
        "StableClosureFreeVariableFact",
        "StableExplicitCaptureMode",
        "StableExplicitCaptureBindingFact",
        "StableExplicitClosureCaptureFact",
        "BoundOwnerBody",
    },
    "S2E": {"ModuleBindingAllocationPlan", "OwnerAllocationRange"},
}
RECORD_TASKS = {
    name: (task, "S3", task)
    for task, names in S2_RECORDS.items()
    for name in names
}
RECORD_TASKS.update(
    {
        "BinderIdentifierDiagnosticArguments": ("S6", "S6", "S6"),
        "BinderNamespaceDiagnosticArguments": ("S6", "S6", "S6"),
        "ActiveCompilationUnitMembership": ("I2", "I2", "I2"),
        "ActiveImplementationMembershipRecord": ("I2", "I2", "I2"),
        "ImplementationGenericAuthority": ("I2", "I2", "I2"),
        "ActiveGenericParameterMembership": ("I2", "I2", "I2"),
        "ActiveCallableParameterMembershipRecord": ("I2", "I2", "I2"),
        "CompleteRootIdentityReadiness": ("I2", "I2", "I2"),
        "ActiveMembershipResult<ActiveCompilationUnitMembership>": ("I2", "I2", "I2"),
        "ActiveMembershipResult<CrateKey>": ("I2", "I2", "I2"),
        "ActiveMembershipResult<SourceFileKey>": ("I2", "I2", "I2"),
        "ActiveMembershipResult<ModuleKey>": ("I2", "I2", "I2"),
        "ActiveMembershipResult<DefinitionIdentityRecord>": ("I2", "I2", "I2"),
        "ActiveMembershipResult<ActiveImplementationMembershipRecord>": ("I2", "I2", "I2"),
        "ActiveMembershipResult<ActiveGenericParameterMembership>": ("I2", "I2", "I2"),
        "ActiveMembershipResult<ActiveCallableParameterMembershipRecord>": ("I2", "I2", "I2"),
        "CanonicalCompilationRootRecord": ("Q3", "Q3", "R30_13"),
        "CanonicalTargetSelectionRecord": ("Q3", "Q3", "R30_13"),
        "CanonicalLanguageOptionsRecord": ("Q3", "Q3", "R30_13"),
        "CanonicalPackageCompilationRequest": ("Q3", "Q3", "R30_13"),
        "CompleteCompilationContextAuthority": ("I1A", "I1A", "I1A"),
        "VerifiedCoreDistributionInputPayload": ("T1", "T1", "T1"),
        "VerifiedModuleGraphInputPayload": ("T1", "T1", "T1"),
        "ContextualIdentityAuthorityInputPayload": ("T1", "T1", "T1"),
        "StableMaterializedDependencyWitness": ("M1", "M1", "M1"),
        "MaterializedModuleGraphWitness": ("M1", "M1", "M1"),
    }
)
SUM_TASKS = {
    "StableHeaderSite": ("S2B", "S3", "S2B"),
    "StableScopeOwnerKey": ("S2B", "S3", "S2B"),
    "StableBindingTargetKey": ("S2B", "S3", "S2B"),
    "StableNodeSyntaxRoot": ("S2B", "S3", "S2B"),
    "StableFailedLookupOutcome": ("S2C", "S3", "S2C"),
    "StableSelfOwner": ("S2D", "S3", "S2D"),
    "StableLabelTarget": ("S2D", "S3", "S2D"),
    "StableControlTarget": ("S2D", "S3", "S2D"),
    "BinderQueryResult": ("S2B", "S3", "S2B"),
    "BinderQueryOwner": ("S2B", "S3", "S2B"),
    "DiagnosticPhaseOrQueryKind": ("S6", "S6", "S6"),
    "DiagnosticEmitterSite": ("S6", "S6", "S6"),
    "ActiveMembershipResult": ("I2", "I2", "I2"),
}
SUM_VARIANTS = {
    "StableHeaderSite": ("DefinitionAuthoritySite", "ImplementationOccurrenceSite"),
    "StableScopeOwnerKey": (
        "ModuleScope",
        "DefinitionScope",
        "ImplementationOccurrenceScope",
        "BodyScope",
    ),
    "StableBindingTargetKey": (
        "Definition",
        "Implementation",
        "Module",
        "SemanticImport",
        "OwnerLocal",
        "AnonymousOwner",
        "GenericParameter",
        "CallableParameter",
    ),
    "StableNodeSyntaxRoot": ("ModuleBody", "DefinitionHeader", "ImplementationHeader"),
    "StableFailedLookupOutcome": ("Missing", "NamespaceMismatch", "Ambiguous"),
    "StableSelfOwner": ("Nominal", "Interface", "ImplementationOccurrence"),
    "StableLabelTarget": ("Block", "Loop"),
    "StableControlTarget": ("ExplicitLabel", "Loop", "Match"),
    "BinderQueryResult": ("Value", "SourceRejected", "KeyRejected"),
    "BinderQueryOwner": ("Module", "DefinitionHeader", "ImplementationHeader", "Body"),
    "DiagnosticPhaseOrQueryKind": (
        "Source",
        "Package",
        "BuildScript",
        "Module",
        "ToolchainModuleRootReservation",
        "CoreFailureProducer",
        "Binder",
    ),
    "DiagnosticEmitterSite": (
        "Source",
        "Package",
        "BuildScript",
        "Module",
        "ToolchainModuleRootReservation",
        "CoreLibrary",
        "Binder",
    ),
    "ActiveMembershipResult": ("Active", "Inactive"),
}
ENUMS = {
    "DefinitionBodyDisposition": (("NoExecutableBody", "ExecutableBody"), ("S2B", "S3", "S2B")),
    "ImplementationSourceForm": (("Ordinary", "BodylessMarker"), ("S2B", "S3", "S2B")),
    "ScopeRole": (
        ("Declaration", "Generic", "Parameters", "Members", "Implementation"),
        ("S2B", "S3", "S2B"),
    ),
    "StableExplicitCaptureMode": (("ByValue", "ByReference", "This"), ("S2D", "S3", "S2D")),
    "BinderKeyFailureKind": (
        (
            "MissingSelectedModuleSource",
            "InactiveOwner",
            "ForeignOwner",
            "DefinitionWithoutBody",
            "BoundaryMismatch",
            "NonSelectedSource",
            "CrossBoundaryPath",
        ),
        ("S2B", "S3", "S2B"),
    ),
    "BinderDiagnosticProducer": (("BindModuleSkeleton", "BindOwnerBody"), ("S6", "S6", "S6")),
    "BinderDiagnosticEmitter": (
        ("Declaration", "Lookup", "ControlTransfer", "ContextualSelf"),
        ("S6", "S6", "S6"),
    ),
    "IdentityDiagnosticPhase": (("IdentityAdmission",), ("S6", "S6", "S6")),
    "IdentityDiagnosticEmitter": (
        (
            "DuplicateBound",
            "DefinitionIdentityCollision",
            "ConstantExpressionNotAllowed",
            "DuplicateGenericParameter",
        ),
        ("S6", "S6", "S6"),
    ),
    "DiagnosticSecondaryRole": (("PreviousDeclaration",), ("S6", "S6", "S6")),
}
QUERY_TASKS = {
    **{name: ("B1",) * 4 for name in (
        "DefinitionHeaderSyntax",
        "ImplementationOccurrenceHeaderSyntax",
        "ModuleExportNames",
        "ExportedBinding",
        "DefinitionBindingHeader",
        "ImplementationBindingHeader",
        "ScopeNameBucket",
        "ImportTarget",
        "BindingVisibility",
        "BindModuleSkeleton",
    )},
    **{name: ("I2",) * 4 for name in (
        "ActiveCompilationUnitMembership",
        "ActiveCrateMembership",
        "ActiveSourceMembership",
        "ActiveModuleMembership",
        "ActiveDefinitionMembership",
        "ActiveImplementationMembership",
        "ActiveGenericParameterMembership",
        "ActiveCallableParameterMembership",
    )},
    **{
        name: ("B2",) * 4
        for name in ("ModuleBodyOwners", "ModuleDiagnosticFacts", "BindOwnerBody")
    },
    "ModuleBindingAllocationPlan": ("B4",) * 4,
}
INPUT_TASKS = {
    "CompleteCompilationContextAuthorityInput": ("I1A", "T1", "I1A", "I1A"),
    "ActiveDefinitionAuthorityInput": ("I2", "T1", "I2", "I2"),
    "ActiveImplementationAuthorityInput": ("I2", "T1", "I2", "I2"),
    "ActiveGenericParameterAuthorityInput": ("I2", "T1", "I2", "I2"),
    "ActiveCallableParameterAuthorityInput": ("I2", "T1", "I2", "I2"),
    "CompleteRootIdentityReadiness": ("I2", "T1", "I2", "I2"),
}
CAPABILITIES = {
    "ModuleDependencyProvenance": (
        "zom.query.module-dependency-provenance",
        "ModuleKey",
        "ModuleDependencyProvenanceMap",
        "ModuleDependencyProvenanceProvider",
        "ModuleDependencyProvenanceVerifier",
        ("R28_16A", "R28_16A", "R28_16A", "R28_16B"),
    ),
    "MaterializeModuleGraph": (
        "zom.query.materialize-module-graph",
        "CompilationRootSetQueryKey",
        "MaterializedModuleGraph",
        "MaterializeModuleGraphProvider",
        "MaterializeModuleGraphVerifier",
        ("M1", "M1", "M1", "M1"),
    ),
    "MaterializeModuleSkeleton": (
        "zom.query.materialize-module-skeleton",
        "ContextualModuleKey",
        "MaterializedModuleSkeleton",
        "MaterializeModuleSkeletonProvider",
        "MaterializeModuleSkeletonVerifier",
        ("M2", "M2", "M2", "M2"),
    ),
    "MaterializeOwnerBody": (
        "zom.query.materialize-owner-body",
        "ContextualBodyOwnerKey",
        "MaterializedOwnerBody",
        "MaterializeOwnerBodyProvider",
        "MaterializeOwnerBodyVerifier",
        ("M3", "M3", "M3", "M3"),
    ),
    "VerifyBoundModule": (
        "zom.query.verify-bound-module",
        "ContextualModuleKey",
        "VerifiedBoundModule",
        "VerifyBoundModuleProvider",
        "VerifyBoundModuleVerifier",
        ("M5", "M5", "M5", "M5"),
    ),
}
Q3_FIELDS = {
    "CanonicalCompilationRootRecord": (
        ("package", "PackageKey"),
        ("targetKind", "CrateTargetKind"),
        ("targetName", "TargetName"),
        ("editionYear", "uint32"),
        ("requiresBuildScript", "bool"),
        ("sourcePath", "CanonicalRelativePath"),
    ),
    "CanonicalTargetSelectionRecord": (
        ("registryRevision", "Sha256Digest"),
        ("profile", "RegisteredTargetProfileName"),
        ("semanticProjection", "CanonicalTargetSpecificationKey"),
        ("panicStrategy", "PackagePanicStrategy"),
    ),
    "CanonicalLanguageOptionsRecord": (
        ("useUnicode", "bool"),
        ("allowDollarIdentifiers", "bool"),
        ("supportRegexLiterals", "bool"),
    ),
    "CanonicalPackageCompilationRequest": (
        ("roots", "CanonicalNonEmptySequence<CanonicalCompilationRootRecord>"),
        ("hostTarget", "CanonicalTargetSelectionRecord"),
        ("target", "CanonicalTargetSelectionRecord"),
        ("languageOptions", "CanonicalLanguageOptionsRecord"),
        ("lockMode", "PackageLockMode"),
    ),
}
Q3_RECORDS = {
    "CanonicalCompilationRootRecord": (
        "zom.input.canonical-compilation-root",
        "domain|truncated|trailing|field|enum|bool|identity",
    ),
    "CanonicalTargetSelectionRecord": (
        "zom.input.canonical-target-selection",
        "domain|truncated|trailing|field|digest|profile|target|enum",
    ),
    "CanonicalLanguageOptionsRecord": (
        "zom.input.canonical-language-options",
        "domain|truncated|trailing|field|bool",
    ),
    "CanonicalPackageCompilationRequest": (
        "zom.input.canonical-package-compilation-request",
        "domain|truncated|trailing|field|duplicate|reordered|enum|max-count",
    ),
}
DIAGNOSTICS = {
    "Missing": (
        "ZOM3001",
        "BinderIdentifierDiagnosticArguments",
        "None",
        "None",
        "0",
        "0",
        "DiagnosticFactTest.BinderLookupRejectsExactMappingMutations",
    ),
    "NamespaceMismatch": (
        "ZOM3002",
        "BinderNamespaceDiagnosticArguments",
        "None",
        "None",
        "0",
        "0",
        "DiagnosticFactTest.BinderLookupRejectsExactMappingMutations",
    ),
    "Ambiguous": (
        "ZOM3028",
        "BinderIdentifierDiagnosticArguments",
        "None",
        "None",
        "0",
        "0",
        "DiagnosticFactTest.BinderLookupRejectsExactMappingMutations",
    ),
    "ConstantExpressionNotAllowed": (
        "ZOM4079",
        "None",
        "None",
        "None",
        "0",
        "0",
        "DiagnosticFactTest.IdentityAdmissionRejectsExactMappingMutations",
    ),
    "DuplicateGenericParameter": (
        "ZOM3010",
        "BinderIdentifierDiagnosticArguments",
        "ZOM3017",
        "PreviousDeclaration",
        "1",
        "0",
        "DiagnosticFactTest.IdentityAdmissionRejectsExactMappingMutations",
    ),
}
DIAGNOSTIC_SUM_FIELDS = {
    ("DiagnosticPhaseOrQueryKind", "ToolchainModuleRootReservation", "producer"): (
        "ToolchainModuleRootReservationProducer"
    ),
    ("DiagnosticPhaseOrQueryKind", "CoreFailureProducer", "producer"): "CoreFailureProducer",
    ("DiagnosticPhaseOrQueryKind", "Binder", "producer"): "BinderDiagnosticProducer",
    ("DiagnosticEmitterSite", "ToolchainModuleRootReservation", "emitter"): (
        "ToolchainModuleRootReservationEmitter"
    ),
    ("DiagnosticEmitterSite", "CoreLibrary", "emitter"): "CoreLibraryDiagnosticEmitter",
    ("DiagnosticEmitterSite", "Binder", "emitter"): "BinderDiagnosticEmitter",
}


@dataclass(frozen=True)
class Row:
    kind: str
    macro: str
    args: tuple[str, ...]
    line: int
    start: int
    end: int


def atom(value: str) -> str:
    return re.sub(r"\s+", "", value)


def quoted(value: str) -> str | None:
    pieces = re.findall(r'"(?:\\.|[^"\\])*"', value, re.DOTALL)
    if not pieces or re.sub(r'"(?:\\.|[^"\\])*"', "", value, flags=re.DOTALL).strip():
        return None
    try:
        return "".join(ast.literal_eval(piece) for piece in pieces)
    except (SyntaxError, ValueError):
        return None


def strip_preprocessor(text: str) -> str:
    output: list[str] = []
    continuation = False
    for line in text.splitlines(keepends=True):
        directive = continuation or line.lstrip().startswith("#")
        continuation = directive and line.rstrip().endswith("\\")
        if directive:
            output.append("".join("\n" if char == "\n" else " " for char in line))
        else:
            output.append(line)
    return "".join(output)


def mask_comments(text: str) -> str:
    output = list(text)
    index = 0
    while index < len(text):
        if text.startswith("//", index):
            end = text.find("\n", index)
            end = len(text) if end < 0 else end
            output[index:end] = " " * (end - index)
            index = end
        elif text.startswith("/*", index):
            end = text.find("*/", index + 2)
            end = len(text) - 2 if end < 0 else end
            for offset in range(index, end + 2):
                if output[offset] != "\n":
                    output[offset] = " "
            index = end + 2
        elif text[index] == '"':
            index += 1
            while index < len(text):
                if text[index] == "\\":
                    index += 2
                elif text[index] == '"':
                    index += 1
                    break
                else:
                    index += 1
        else:
            index += 1
    return "".join(output)


def split_arguments(body: str) -> tuple[str, ...]:
    args: list[str] = []
    start = 0
    stack: list[str] = []
    pairs = {")": "(", "]": "[", "}": "{", ">": "<"}
    index = 0
    while index < len(body):
        char = body[index]
        if char == '"':
            index += 1
            while index < len(body):
                if body[index] == "\\":
                    index += 2
                elif body[index] == '"':
                    break
                else:
                    index += 1
        elif char in "([{<":
            stack.append(char)
        elif char in ")]}>":
            if stack and stack[-1] == pairs[char]:
                stack.pop()
        elif char == "," and not stack:
            args.append(body[start:index].strip())
            start = index + 1
        index += 1
    args.append(body[start:].strip())
    return tuple(args)


def logical_directives(text: str) -> list[str]:
    lines = text.splitlines(keepends=True)
    directives: list[str] = []
    index = 0
    while index < len(lines):
        if not lines[index].lstrip().startswith("#"):
            index += 1
            continue
        parts = [lines[index]]
        while parts[-1].rstrip().endswith("\\") and index + 1 < len(lines):
            index += 1
            parts.append(lines[index])
        directives.append(re.sub(r"\\\s*\n", " ", "".join(parts)).strip())
        index += 1
    return directives


def check_macro_contract(text: str, errors: list[str]) -> None:
    signatures: dict[str, list[tuple[str, ...]]] = defaultdict(list)
    directives = logical_directives(text)
    pattern = re.compile(
        r"^#define\s+ZOM_STABLE_BINDING_([A-Z_]+)\s*\((.*?)\)\s*$",
        re.DOTALL,
    )
    for directive in directives:
        match = pattern.match(directive)
        if match and match.group(1) in KINDS:
            signatures[match.group(1)].append(
                tuple(atom(argument) for argument in split_arguments(match.group(2)))
            )
    for macro, expected in MACRO_SIGNATURES.items():
        actual = signatures.get(macro, [])
        if actual != [expected]:
            errors.append(
                f"macro {macro}: signature must be ({', '.join(expected)}), found {actual}"
            )
        owned = f"ZOM_STABLE_BINDING_{macro}_OWNED"
        entity = f"ZOM_STABLE_BINDING_{macro}"
        required = {
            f"#define {owned}": "owned marker definition",
            f"#ifdef {owned}": "owned cleanup guard",
            f"#undef {entity}": "entity cleanup undef",
            f"#undef {owned}": "owned marker cleanup undef",
        }
        for directive, label in required.items():
            if directives.count(directive) != 1:
                errors.append(f"macro {macro}: expected one {label}")


def parse(text: str) -> tuple[list[Row], list[str]]:
    source = mask_comments(strip_preprocessor(text))
    rows: list[Row] = []
    errors: list[str] = []
    pattern = re.compile(r"ZOM_STABLE_BINDING_([A-Z_]+)\s*\(")
    for match in pattern.finditer(source):
        macro = match.group(1)
        depth = 1
        index = match.end()
        in_string = False
        while index < len(source) and depth:
            char = source[index]
            if in_string:
                if char == "\\":
                    index += 2
                    continue
                if char == '"':
                    in_string = False
            elif char == '"':
                in_string = True
            elif char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
            index += 1
        line = source.count("\n", 0, match.start()) + 1
        if depth:
            errors.append(f"line {line}: unbalanced {macro} invocation")
            continue
        kind = KINDS.get(macro)
        if kind is None:
            errors.append(f"line {line}: unknown entity kind {macro}")
            continue
        rows.append(
            Row(
                kind=kind,
                macro=macro,
                args=split_arguments(source[match.end():index - 1]),
                line=line,
                start=match.start(),
                end=index,
            )
        )
    if not rows:
        errors.append("schema contains no inventory rows")
    return rows, errors


def duplicate_values(values: list[str]) -> set[str]:
    counts = Counter(values)
    return {value for value, count in counts.items() if count != 1}


def parse_int(row: Row, column: int, label: str, errors: list[str]) -> int | None:
    value = atom(row.args[column])
    try:
        return int(value, 0)
    except ValueError:
        errors.append(f"line {row.line}: {label} is not an integer: {value}")
        return None


def task_tuple(row: Row) -> tuple[str, ...]:
    return tuple(atom(row.args[index]) for index in TASK_COLUMNS[row.kind])


def bind_payload(template: str, metavariable: str, concrete: str) -> str | None:
    marker = re.compile(
        rf"(?<![A-Za-z0-9_]){re.escape(metavariable)}(?![A-Za-z0-9_])"
    )
    parts = marker.split(template)
    if len(parts) == 1:
        return None
    pattern = "^" + "(.+?)".join(re.escape(part) for part in parts) + "$"
    match = re.match(pattern, concrete)
    if match is None or not match.groups():
        return None
    payloads = set(match.groups())
    if len(payloads) != 1:
        return None
    return next(iter(payloads))


def derive_failure_alternatives(
    grouped: dict[str, list[Row]], errors: list[str]
) -> tuple[str, ...]:
    conditional: list[tuple[int, str, str, str]] = []
    condition_pattern = re.compile(
        r"^FailureAlternative<([A-Za-z_][A-Za-z0-9_]*)"
        r"<([A-Za-z_][A-Za-z0-9_]*)>>$"
    )
    for row in grouped["RuntimeSumVariant"]:
        if atom(row.args[0]) != "CapabilityDemandResult":
            continue
        condition = atom(row.args[3])
        if condition == "Always":
            continue
        match = condition_pattern.match(condition)
        if match is None:
            errors.append(
                f"line {row.line}: invalid conditional runtime-sum topology {condition}"
            )
            continue
        try:
            tag = int(atom(row.args[2]), 0)
        except ValueError:
            continue
        conditional.append((tag, atom(row.args[1]), match.group(1), match.group(2)))

    alternatives: list[str] = []
    for _, variant, wrapper, metavariable in sorted(conditional):
        generic_fields = {
            (atom(row.args[2]), atom(row.args[3])): atom(row.args[4])
            for row in grouped["RuntimeVariantField"]
            if atom(row.args[0]) == "CapabilityDemandResult"
            and atom(row.args[1]) == variant
        }
        concrete_fields = {
            (atom(row.args[2]), atom(row.args[3])): atom(row.args[4])
            for row in grouped["VariantField"]
            if atom(row.args[0]) == "BinderQueryResult"
            and atom(row.args[1]) == variant
        }
        if not generic_fields or generic_fields.keys() != concrete_fields.keys():
            errors.append(
                f"runtime variant {variant}: cannot derive payload from BinderQueryResult fields"
            )
            continue
        bindings = {
            binding
            for key, template in generic_fields.items()
            if (binding := bind_payload(template, metavariable, concrete_fields[key]))
            is not None
        }
        if len(bindings) != 1 or any(
            bind_payload(template, metavariable, concrete_fields[key]) is None
            for key, template in generic_fields.items()
        ):
            errors.append(
                f"runtime variant {variant}: payload metavariable {metavariable} "
                "does not have one schema-derived binding"
            )
            continue
        alternatives.append(f"{wrapper}<{next(iter(bindings))}>")
    return tuple(alternatives)


def rows_by_kind(rows: list[Row]) -> dict[str, list[Row]]:
    result: dict[str, list[Row]] = defaultdict(list)
    for row in rows:
        result[row.kind].append(row)
    return result


def exact_names(kind: str, rows: list[Row], expected: set[str], errors: list[str]) -> None:
    actual = {atom(row.args[0]) for row in rows}
    for name in sorted(expected - actual):
        errors.append(f"{kind}: missing required row {name}")
    for name in sorted(actual - expected):
        errors.append(f"{kind}: unexpected row {name}")


def check_dense(rows: list[Row], key_columns: tuple[int, ...], value_column: int, start: int,
                label: str, errors: list[str]) -> None:
    groups: dict[tuple[str, ...], list[int]] = defaultdict(list)
    for row in rows:
        value = parse_int(row, value_column, label, errors)
        if value is not None:
            groups[tuple(atom(row.args[index]) for index in key_columns)].append(value)
    for key, values in groups.items():
        expected = list(range(start, start + len(values)))
        if sorted(values) != expected:
            errors.append(f"{label} for {'.'.join(key)} must be unique and dense from {start}")


def validate(text: str) -> list[str]:
    rows, errors = parse(text)
    check_macro_contract(text, errors)
    grouped = rows_by_kind(rows)
    for kind, count in EXPECTED_COUNTS.items():
        if len(grouped[kind]) != count:
            errors.append(f"{kind}: expected {count} rows, found {len(grouped[kind])}")
    valid_rows: list[Row] = []
    for row in rows:
        expected = ARITIES[row.kind]
        if len(row.args) != expected:
            errors.append(
                f"line {row.line}: {row.kind} has {len(row.args)} arguments instead of {expected}"
            )
        else:
            valid_rows.append(row)
    grouped = rows_by_kind(valid_rows)

    stable_header_provenance = {
        "StableHeaderGenericParameter": (
            "StableDefinitionHeaderProducer",
            "StableHeaderVerifier",
        ),
        "StableHeaderCallableParameter": (
            "StableDefinitionHeaderProducer",
            "StableHeaderVerifier",
        ),
        "StableDefinitionHeader": (
            "StableDefinitionHeaderProducer",
            "StableHeaderVerifier",
        ),
        "StableImplementationOccurrenceHeader": (
            "StableImplementationOccurrenceHeaderProducer",
            "StableHeaderVerifier",
        ),
    }
    for row in grouped["Record"]:
        name = atom(row.args[0])
        if name not in stable_header_provenance:
            continue
        producer, verifier = stable_header_provenance[name]
        if atom(row.args[3]) != producer or atom(row.args[4]) != verifier:
            errors.append(
                f"line {row.line}: {name} must name its RFC 0027 producer and verifier"
            )

    for row in valid_rows:
        for index in TASK_COLUMNS.get(row.kind, ()):
            task = atom(row.args[index])
            if task not in TASKS:
                errors.append(f"line {row.line}: unknown or missing artifact task {task!r}")
        mutation_index = MUTATION_COLUMN.get(row.kind)
        if mutation_index is not None:
            mutations = quoted(row.args[mutation_index])
            if not mutations:
                errors.append(f"line {row.line}: mutation inventory must be a non-empty string")
            else:
                unknown = set(mutations.split("|")) - KNOWN_MUTATIONS
                if unknown:
                    errors.append(
                        f"line {row.line}: unknown mutations {','.join(sorted(unknown))}"
                    )
            test = quoted(row.args[TEST_COLUMN[row.kind]])
            if not test:
                errors.append(f"line {row.line}: executable mutation test is missing")
    for kind, columns in {
        "Record": (3, 4),
        "Query": (5, 6),
        "Input": (4,),
        "CapabilityQuery": (5, 6),
    }.items():
        for row in grouped[kind]:
            for column in columns:
                if not atom(row.args[column]) or atom(row.args[column]) == "None":
                    errors.append(f"line {row.line}: {kind} producer or verifier is missing")

    for kind in ("Record", "Query", "Input", "CapabilityQuery", "Digest"):
        domains = [quoted(row.args[1]) for row in grouped[kind]]
        for domain in sorted(duplicate_values([value for value in domains if value is not None])):
            errors.append(f"{kind}: duplicate domain {domain}")
        for row, domain in zip(grouped[kind], domains):
            if not domain:
                errors.append(f"line {row.line}: {kind} domain must be a non-empty string")
    descriptor_domains = [
        quoted(row.args[1])
        for kind in ("Query", "Input", "CapabilityQuery")
        for row in grouped[kind]
    ]
    for domain in sorted(
        duplicate_values([value for value in descriptor_domains if value is not None])
    ):
        errors.append(f"descriptor: duplicate domain {domain}")

    bound_rows = grouped["Bound"]
    bound_names = {atom(row.args[0]) for row in bound_rows}
    for duplicate in sorted(duplicate_values([atom(row.args[0]) for row in bound_rows])):
        errors.append(f"Bound: duplicate name {duplicate}")
    for row in bound_rows:
        limit = atom(row.args[1])
        if limit not in {"UINT32_MAX", "SIZE_MAX"}:
            try:
                int(limit, 0)
            except ValueError:
                errors.append(f"line {row.line}: invalid bound limit {limit}")
        if not quoted(row.args[2]):
            errors.append(f"line {row.line}: bound rule must be a non-empty string")
    for kind, maximum_column in (("Record", 2), ("NestedRecord", 3)):
        for row in grouped[kind]:
            maximum = atom(row.args[maximum_column])
            if maximum not in bound_names:
                errors.append(f"line {row.line}: unknown maximum bound {maximum}")
    for row in grouped["FieldLimit"]:
        bound = atom(row.args[1])
        if bound not in bound_names:
            errors.append(f"line {row.line}: unknown field-limit bound {bound}")

    owner_kinds = ("Record", "NestedRecord", "Sum", "RuntimeSum")
    owners: dict[str, dict[str, list[Row]]] = {}
    for kind in owner_kinds:
        owners[kind] = defaultdict(list)
        for row in grouped[kind]:
            owners[kind][atom(row.args[0])].append(row)
        for name, matches in owners[kind].items():
            if len(matches) != 1:
                errors.append(f"{kind}: {name} has {len(matches)} owner rows")

    def require_owner(row: Row, owner_kind: str, name_column: int = 0) -> None:
        name = atom(row.args[name_column])
        if len(owners[owner_kind].get(name, ())) != 1:
            errors.append(f"line {row.line}: {row.kind} has no unique {owner_kind} owner {name}")

    for row in grouped["NestedField"]:
        require_owner(row, "NestedRecord")
    for row in grouped["Field"]:
        require_owner(row, "Record")
    for row in grouped["SumVariant"] + grouped["VariantField"]:
        require_owner(row, "Sum")
    for row in grouped["RuntimeSumVariant"] + grouped["RuntimeVariantField"]:
        require_owner(row, "RuntimeSum")
    for row in grouped["InlineSumVariant"] + grouped["InlineSumVariantField"]:
        require_owner(row, "Record")

    check_dense(grouped["EnumValue"], (0,), 2, 1, "enum tag", errors)
    check_dense(grouped["SumVariant"], (0,), 2, 1, "sum tag", errors)
    check_dense(grouped["RuntimeSumVariant"], (0,), 2, 1, "runtime-sum tag", errors)
    check_dense(grouped["InlineSumVariant"], (0, 1, 2), 4, 1, "inline-sum tag", errors)
    check_dense(grouped["NestedField"], (0,), 1, 0, "nested-field ordinal", errors)
    check_dense(grouped["VariantField"], (0, 1), 2, 0, "variant-field ordinal", errors)
    check_dense(grouped["RuntimeVariantField"], (0, 1), 2, 0,
                "runtime-variant-field ordinal", errors)
    check_dense(grouped["InlineSumVariantField"], (0, 1, 2), 3, 0,
                "inline-variant-field ordinal", errors)
    field_positions: dict[str, list[int]] = defaultdict(list)
    for row in grouped["Field"]:
        ordinal = parse_int(row, 1, "field ordinal", errors)
        if ordinal is not None:
            field_positions[atom(row.args[0])].append(ordinal)
    for name, positions in field_positions.items():
        if len(positions) != len(set(positions)):
            errors.append(f"field ordinal for {name} is duplicated")
    inline_positions: dict[str, set[int]] = defaultdict(set)
    inline_fields: dict[tuple[str, str], tuple[int, set[str]]] = {}
    for row in grouped["InlineSumVariant"]:
        ordinal = parse_int(row, 1, "inline field ordinal", errors)
        if ordinal is not None:
            record = atom(row.args[0])
            field = atom(row.args[2])
            variant = atom(row.args[3])
            key = (record, field)
            previous = inline_fields.get(key)
            if previous is not None and previous[0] != ordinal:
                errors.append(f"inline sum {record}.{field} uses contradictory field ordinals")
            variants = set() if previous is None else previous[1]
            if variant in variants:
                errors.append(f"inline sum {record}.{field} duplicates variant {variant}")
            variants.add(variant)
            inline_fields[key] = (ordinal, variants)
            inline_positions[record].add(ordinal)
    for record, positions in inline_positions.items():
        if positions & set(field_positions[record]):
            errors.append(f"record {record} overlaps regular and inline field ordinals")
        field_positions[record].extend(sorted(positions))
    for name, positions in field_positions.items():
        if sorted(positions) != list(range(len(positions))):
            errors.append(f"field ordinal for {name} must be dense from 0")
    sum_variants = {
        (atom(row.args[0]), atom(row.args[1])) for row in grouped["SumVariant"]
    }
    runtime_variants_set = {
        (atom(row.args[0]), atom(row.args[1])) for row in grouped["RuntimeSumVariant"]
    }
    for row in grouped["VariantField"]:
        key = (atom(row.args[0]), atom(row.args[1]))
        if key not in sum_variants:
            errors.append(f"line {row.line}: field refers to missing sum variant {'.'.join(key)}")
    for row in grouped["RuntimeVariantField"]:
        key = (atom(row.args[0]), atom(row.args[1]))
        if key not in runtime_variants_set:
            errors.append(
                f"line {row.line}: field refers to missing runtime variant {'.'.join(key)}"
            )
    for row in grouped["InlineSumVariantField"]:
        key = (atom(row.args[0]), atom(row.args[1]))
        variant = atom(row.args[2])
        if key not in inline_fields or variant not in inline_fields[key][1]:
            errors.append(
                f"line {row.line}: field refers to missing inline variant "
                f"{key[0]}.{key[1]}.{variant}"
            )
    valid_limit_targets = {
        f"{atom(row.args[0])}.{atom(row.args[2])}" for row in grouped["Field"]
    }
    valid_limit_targets.update(
        f"{atom(row.args[0])}.{atom(row.args[2])}" for row in grouped["NestedField"]
    )
    valid_limit_targets.update(
        f"{atom(row.args[0])}.{atom(row.args[1])}.{atom(row.args[3])}"
        for row in grouped["VariantField"] + grouped["RuntimeVariantField"]
    )
    valid_limit_targets.update(
        f"{atom(row.args[0])}.{atom(row.args[1])}.{atom(row.args[2])}."
        f"{atom(row.args[4])}"
        for row in grouped["InlineSumVariantField"]
    )
    valid_limit_targets.update(
        f"{atom(row.args[0])}.result" for row in grouped["Query"]
    )
    for row in grouped["FieldLimit"]:
        target = quoted(row.args[0])
        if not target:
            errors.append(f"line {row.line}: field-limit target must be a non-empty string")
        elif target.replace("[]", "") not in valid_limit_targets:
            errors.append(f"line {row.line}: unknown field-limit target {target}")
    query_names = {atom(row.args[0]) for row in grouped["Query"]}
    capability_names = {atom(row.args[0]) for row in grouped["CapabilityQuery"]}
    for row in grouped["MaterializerPermission"]:
        descriptor = atom(row.args[0])
        membership = atom(row.args[2])
        if descriptor not in capability_names:
            errors.append(
                f"line {row.line}: permission has unknown capability descriptor {descriptor}"
            )
        if membership not in query_names:
            errors.append(
                f"line {row.line}: permission has unknown membership descriptor {membership}"
            )
    for row in grouped["Constraint"]:
        if not quoted(row.args[0]) or not quoted(row.args[1]):
            errors.append(f"line {row.line}: constraint target and rule must be non-empty strings")

    exact_names("Record", grouped["Record"], set(RECORD_TASKS), errors)
    for row in grouped["Record"]:
        name = atom(row.args[0])
        expected = RECORD_TASKS.get(name)
        if expected and task_tuple(row) != expected:
            errors.append(f"Record {name}: task triple must be {expected}")
        if (
            name == "CompleteCompilationContextAuthority"
            and atom(row.args[3])
            != "CompleteCompilationContextAuthority::fromVerified"
        ):
            errors.append(
                "Record CompleteCompilationContextAuthority: producer must be "
                "CompleteCompilationContextAuthority::fromVerified"
            )
    exact_names("NestedRecord", grouped["NestedRecord"], {"CanonicalInputEntry"}, errors)
    for row in grouped["NestedRecord"]:
        if atom(row.args[0]) == "CanonicalInputEntry" and task_tuple(row) != (
            "I1A",
            "I1A",
            "I1A",
        ):
            errors.append("NestedRecord CanonicalInputEntry: task triple must be I1A/I1A/I1A")

    exact_names("Sum", grouped["Sum"], set(SUM_TASKS), errors)
    for row in grouped["Sum"]:
        name = atom(row.args[0])
        expected = SUM_TASKS.get(name)
        if expected and task_tuple(row) != expected:
            errors.append(f"Sum {name}: task triple must be {expected}")
        for record in grouped["Record"]:
            if (
                atom(record.args[0]).split("<", 1)[0] == name
                and task_tuple(record) != task_tuple(row)
            ):
                errors.append(f"Record and Sum ownership disagree for {atom(record.args[0])}")
    actual_variants: dict[str, tuple[str, ...]] = {}
    for name in SUM_VARIANTS:
        matches = sorted(
            (
                (parse_int(row, 2, "sum tag", errors), atom(row.args[1]))
                for row in grouped["SumVariant"]
                if atom(row.args[0]) == name
            ),
            key=lambda item: -1 if item[0] is None else item[0],
        )
        actual_variants[name] = tuple(value for _, value in matches)
    for name, expected in SUM_VARIANTS.items():
        if actual_variants[name] != expected:
            errors.append(f"Sum {name}: variants do not match the accepted inventory")
    diagnostic_sum_fields = {
        (atom(row.args[0]), atom(row.args[1]), atom(row.args[3])): atom(row.args[4])
        for row in grouped["VariantField"]
        if atom(row.args[0]) in {"DiagnosticPhaseOrQueryKind", "DiagnosticEmitterSite"}
    }
    if diagnostic_sum_fields != DIAGNOSTIC_SUM_FIELDS:
        errors.append("diagnostic sum payload fields do not match the accepted inventory")

    exact_names("Enum", grouped["EnumValue"], set(ENUMS), errors)
    for name, (values, tasks) in ENUMS.items():
        matches = sorted(
            (
                (parse_int(row, 2, "enum tag", errors), atom(row.args[1]), task_tuple(row))
                for row in grouped["EnumValue"]
                if atom(row.args[0]) == name
            ),
            key=lambda item: -1 if item[0] is None else item[0],
        )
        if tuple(value for _, value, _ in matches) != values:
            errors.append(f"Enum {name}: values do not match the accepted inventory")
        if any(actual != tasks for _, _, actual in matches):
            errors.append(f"Enum {name}: task triple must be {tasks}")

    exact_names("Query", grouped["Query"], set(QUERY_TASKS), errors)
    for row in grouped["Query"]:
        name = atom(row.args[0])
        expected = QUERY_TASKS.get(name)
        if expected and task_tuple(row) != expected:
            errors.append(f"Query {name}: task columns must be {expected}")
        if name == "BindingVisibility" and atom(row.args[3]) != "Optional<MemberVisibility>":
            errors.append("BindingVisibility result must be Optional<MemberVisibility>")
    local_export_visibility = [
        row
        for row in grouped["Field"]
        if atom(row.args[0]) == "StableLocalExportFact"
        and atom(row.args[2]) == "visibility"
    ]
    if (
        len(local_export_visibility) != 1
        or atom(local_export_visibility[0].args[3]) != "Optional<MemberVisibility>"
    ):
        errors.append(
            "StableLocalExportFact.visibility must be Optional<MemberVisibility>"
        )
    exact_names("Input", grouped["Input"], set(INPUT_TASKS), errors)
    for row in grouped["Input"]:
        name = atom(row.args[0])
        expected = INPUT_TASKS.get(name)
        if expected and task_tuple(row) != expected:
            errors.append(f"Input {name}: task columns must be {expected}")

    exact_names("CapabilityQuery", grouped["CapabilityQuery"], set(CAPABILITIES), errors)
    runtime_rows = grouped["RuntimeSum"]
    if len(runtime_rows) == 1:
        row = runtime_rows[0]
        if atom(row.args[0]) != "CapabilityDemandResult":
            errors.append("runtime sum must be CapabilityDemandResult")
        if task_tuple(row) != ("R29_13A", "R29_13C"):
            errors.append("CapabilityDemandResult ownership must be R29_13A/R29_13C")
    runtime_variants = {
        atom(row.args[1]): (atom(row.args[2]), atom(row.args[3]))
        for row in grouped["RuntimeSumVariant"]
        if atom(row.args[0]) == "CapabilityDemandResult"
    }
    expected_runtime_variants = {
        "Published": ("0x01", "Always"),
        "SourceRejected": ("0x02", "FailureAlternative<SourceRejection<Diagnostic>>"),
        "KeyRejected": ("0x03", "FailureAlternative<KeyRejection<KeyFailure>>"),
        "RuntimeRejected": ("0x04", "Always"),
    }
    if runtime_variants != expected_runtime_variants:
        errors.append(
            "CapabilityDemandResult variants or conditions do not match the accepted model"
        )
    runtime_fields = {
        (atom(row.args[1]), atom(row.args[3])): atom(row.args[4])
        for row in grouped["RuntimeVariantField"]
        if atom(row.args[0]) == "CapabilityDemandResult"
    }
    expected_runtime_fields = {
        ("Published", "lease"): "QueryCapabilityLease<constDescriptor::Capability>",
        ("SourceRejected", "diagnostics"): "CanonicalNonEmptySequence<Diagnostic>",
        ("KeyRejected", "failure"): "KeyFailure",
        ("RuntimeRejected", "failure"): "QueryRuntimeFailure",
    }
    if runtime_fields != expected_runtime_fields:
        errors.append(
            "CapabilityDemandResult fields do not match the accepted generic payload model"
        )
    derived_failures = derive_failure_alternatives(grouped, errors)
    for row in grouped["CapabilityQuery"]:
        name = atom(row.args[0])
        expected = CAPABILITIES.get(name)
        if expected is None:
            continue
        domain, key, capability, producer, verifier, tasks = expected
        actual = (
            quoted(row.args[1]),
            atom(row.args[2]),
            atom(row.args[4]),
            atom(row.args[5]),
            atom(row.args[6]),
        )
        if actual != (domain, key, capability, producer, verifier):
            errors.append(f"CapabilityQuery {name}: descriptor contract drift")
        if atom(row.args[3]) != f"CapabilityDemandResult<{name}>":
            errors.append(f"CapabilityQuery {name}: result must be CapabilityDemandResult<{name}>")
        failures = atom(row.args[7])
        if tuple(failures.split("|")) != derived_failures:
            errors.append(
                f"CapabilityQuery {name}: failure alternatives do not match the "
                "schema-derived runtime substitutions"
            )
        if task_tuple(row) != tasks:
            errors.append(f"CapabilityQuery {name}: task columns must be {tasks}")

    bounds = {
        atom(row.args[0]): (atom(row.args[1]), quoted(row.args[2]))
        for row in grouped["Bound"]
    }
    if bounds.get("CanonicalPackageRecordBytes") != ("UINT32_MAX", "CompleteRecordBytes"):
        errors.append(
            "CanonicalPackageRecordBytes must be UINT32_MAX with CompleteRecordBytes"
        )
    if bounds.get("CanonicalInputSequenceRecords") != ("UINT32_MAX", "SequenceCount"):
        errors.append(
            "CanonicalInputSequenceRecords must be UINT32_MAX with SequenceCount"
        )
    if bounds.get("CanonicalInputValueBytes") != ("SIZE_MAX", "RemainingInputBytes"):
        errors.append(
            "CanonicalInputValueBytes must be SIZE_MAX with RemainingInputBytes"
        )
    if bounds.get("TargetProfileBytes") != ("255", "NfcUtf8Bytes"):
        errors.append("TargetProfileBytes must be 255 with NfcUtf8Bytes")
    record_index = {atom(row.args[0]): row for row in grouped["Record"]}
    for name, (domain, mutations) in Q3_RECORDS.items():
        row = record_index.get(name)
        if row is None:
            continue
        expected = (
            domain,
            "CanonicalPackageRecordBytes",
            "CanonicalPackageCompilationRequest::fromVerified",
            "CanonicalPackageCompilationRequestProjectionVerifier",
            mutations,
            "Canonical package request schema rejects every declared mutation",
        )
        actual = (
            quoted(row.args[1]),
            atom(row.args[2]),
            atom(row.args[3]),
            atom(row.args[4]),
            quoted(row.args[8]),
            quoted(row.args[9]),
        )
        if actual != expected:
            errors.append(f"Record {name}: Q3 contract drift")
    for name, expected in Q3_FIELDS.items():
        actual = sorted(
            (
                (parse_int(row, 1, "field ordinal", errors), atom(row.args[2]), atom(row.args[3]))
                for row in grouped["Field"]
                if atom(row.args[0]) == name
            ),
            key=lambda item: -1 if item[0] is None else item[0],
        )
        if tuple((field, field_type) for _, field, field_type in actual) != expected:
            errors.append(f"Record {name}: Q3 fields do not match the accepted inventory")
    limits = {(quoted(row.args[0]) or atom(row.args[0])): atom(row.args[1])
              for row in grouped["FieldLimit"]}
    if limits.get("CanonicalTargetSelectionRecord.profile") != "TargetProfileBytes":
        errors.append("CanonicalTargetSelectionRecord.profile must use TargetProfileBytes")
    if limits.get("CanonicalPackageCompilationRequest.roots") != "CanonicalInputSequenceRecords":
        errors.append(
            "CanonicalPackageCompilationRequest.roots must use CanonicalInputSequenceRecords"
        )

    exact_names("DiagnosticMapping", grouped["DiagnosticMapping"], set(DIAGNOSTICS), errors)
    expected_mutations = "code|arguments|secondary-code|secondary-role|secondary-count|fix-it-count"
    for row in grouped["DiagnosticMapping"]:
        name = atom(row.args[0])
        expected = DIAGNOSTICS.get(name)
        if expected is None:
            continue
        actual = tuple(atom(row.args[index]) for index in range(1, 7)) + (quoted(row.args[10]),)
        if actual != expected:
            errors.append(f"DiagnosticMapping {name}: exact mapping drift")
        if task_tuple(row) != ("S6", "S6"):
            errors.append(f"DiagnosticMapping {name}: tasks must be S6/S6")
        if quoted(row.args[9]) != expected_mutations:
            errors.append(f"DiagnosticMapping {name}: mutation inventory drift")
    digest_rows = grouped["Digest"]
    if len(digest_rows) == 1:
        row = digest_rows[0]
        if atom(row.args[0]) != "CanonicalInputPayloadDigest" or task_tuple(row) != ("T1", "T1"):
            errors.append("CanonicalInputPayloadDigest ownership must be T1/T1")
    return errors


def read_repository_inputs() -> dict[str, str]:
    inputs: dict[str, str] = {}
    for name, path in REPOSITORY_INPUTS.items():
        try:
            inputs[name] = path.read_text(encoding="utf-8")
        except OSError as error:
            inputs[name] = ""
            print(f"error: cannot read {path.relative_to(ROOT)}: {error}", file=sys.stderr)
    return inputs


def schema_entity_names(rows: list[Row], task_column: int,
                        accepted_tasks: set[str]) -> set[str]:
    names: set[str] = set()
    for row in rows:
        if atom(row.args[task_column]) in accepted_tasks:
            names.add(atom(row.args[0]).split("<", 1)[0])
    return names


def validate_repository_wiring(text: str, inputs: dict[str, str]) -> list[str]:
    errors: list[str] = []
    missing_inputs = sorted(set(REPOSITORY_INPUTS) - set(inputs))
    if missing_inputs:
        return [f"missing repository input {name}" for name in missing_inputs]
    rows, parse_errors = parse(text)
    if parse_errors:
        return parse_errors
    grouped: dict[str, list[Row]] = defaultdict(list)
    for row in rows:
        grouped[row.kind].append(row)

    facts_source = inputs["facts-source"]
    if (
        '#include "zomlang/compiler/binder/stable-binding-schema.def"'
        not in facts_source
        or "#define ZOM_STABLE_BINDING_RECORD" not in facts_source
    ):
        errors.append("stable-binding-facts.cc must directly consume the stable schema")

    for name in ("facts-header", "facts-source", "codec-header", "codec-source"):
        if re.search(r'#include\s+"zomlang/compiler/driver/', inputs[name]):
            errors.append(f"{name} must not include driver headers")

    binder_cmake = inputs["binder-cmake"]
    for source in ("stable-binding-facts.cc", "stable-binding-codec.cc"):
        if binder_cmake.count(f"${{CMAKE_CURRENT_SOURCE_DIR}}/{source}") != 1:
            errors.append(f"Binder target must contain {source} exactly once")

    test_cmake = inputs["test-cmake"]
    required_tests = {
        "stable-binding-facts-test": 'add_ztest_unit_test("stable-binding-facts-test"',
        "stable-binding-schema": "add_test(NAME stable-binding-schema\n",
        "stable-binding-schema-negative": "add_test(NAME stable-binding-schema-negative\n",
        "stable-binding-landing-scope-negative": (
            "add_test(NAME stable-binding-landing-scope-negative\n"
        ),
    }
    for name, registration in required_tests.items():
        if test_cmake.count(registration) != 1:
            errors.append(f"CTest registration {name} must appear exactly once")

    declarations = (
        inputs["facts-header"]
        + inputs["contextual-header"]
        + inputs["metadata-header"]
    )
    native_test = inputs["native-test"] + inputs["contextual-test"]
    s2_tasks = {"S2A", "S2B", "S2C", "S2D", "S2E"}
    s2_entities = schema_entity_names(grouped["Record"], 5, s2_tasks)
    s2_entities.update(schema_entity_names(grouped["NestedRecord"], 4, s2_tasks))
    s2_entities.update(schema_entity_names(grouped["Sum"], 1, s2_tasks))
    s2_entities.update(
        atom(row.args[0])
        for row in grouped["EnumValue"]
        if atom(row.args[3]) in s2_tasks
    )
    for name in sorted(s2_entities):
        if name not in declarations:
            errors.append(f"S2 declaration is not wired for {name}")
        if name not in native_test:
            errors.append(f"S2 native test execution is not wired for {name}")

    codec_sources = (
        inputs["codec-header"] + inputs["codec-source"] + inputs["contextual-source"]
    )
    s3_entities = schema_entity_names(grouped["Record"], 6, {"S3"})
    s3_entities.update(schema_entity_names(grouped["NestedRecord"], 5, {"S3"}))
    s3_entities.update(schema_entity_names(grouped["Sum"], 2, {"S3"}))
    s3_entities.update(
        atom(row.args[0])
        for row in grouped["EnumValue"]
        if atom(row.args[4]) == "S3"
    )
    for name in sorted(s3_entities):
        if name not in codec_sources:
            errors.append(f"S3 codec execution is not wired for {name}")
        if name not in native_test:
            errors.append(f"S3 native wire oracle is not wired for {name}")
    if "independent" not in inputs["native-test"].lower() or "Wire" not in native_test:
        errors.append("S3 native test must retain independent fixed wire oracles")

    prohibited_revision = re.compile(r"(?:^|[^A-Za-z0-9_])V[0-9]+(?:[^A-Za-z0-9_]|$)")
    prohibited_compatibility = re.compile(
        r"\b(?:compat(?:ibility)?|deprecated|legacy|shim|fallback)\b", re.IGNORECASE
    )
    for name in ("facts-header", "facts-source", "codec-header", "codec-source"):
        if prohibited_revision.search(inputs[name]):
            errors.append(f"{name} contains an internal revision suffix")
        if prohibited_compatibility.search(inputs[name]):
            errors.append(f"{name} contains compatibility vocabulary")
    return errors


def validate_live_repository(text: str) -> list[str]:
    errors = validate(text)
    if errors:
        return errors
    return validate_repository_wiring(text, read_repository_inputs())


def replace_row(text: str, row: Row, args: list[str], macro: str | None = None) -> str:
    invocation = f"ZOM_STABLE_BINDING_{macro or row.macro}({', '.join(args)})"
    return text[:row.start] + invocation + text[row.end:]


def find_row(text: str, kind: str, name: str, second: str | None = None) -> Row:
    rows, parse_errors = parse(text)
    if parse_errors:
        raise ValueError("; ".join(parse_errors))
    for row in rows:
        if row.kind != kind or not row.args or atom(row.args[0]) != name:
            continue
        selector_column = 2 if kind == "Field" else 1
        if second is None or (
            len(row.args) > selector_column and atom(row.args[selector_column]) == second
        ):
            return row
    raise ValueError(f"missing self-test fixture {kind} {name} {second or ''}".strip())


def mutate_arg(text: str, kind: str, name: str, column: int, value: str,
               second: str | None = None) -> str:
    row = find_row(text, kind, name, second)
    args = list(row.args)
    args[column] = value
    return replace_row(text, row, args)


def self_test(text: str) -> list[str]:
    baseline = validate_live_repository(text)
    if baseline:
        return ["live schema and repository wiring must pass before self-test mutations run",
                *baseline]
    cases: list[tuple[str, str]] = []

    first = find_row(text, "Record", "StableDefinitionQueryKey")
    second = find_row(text, "Record", "StableImplementationQueryKey")
    cases.append(("duplicate record domain", mutate_arg(
        text, "Record", atom(second.args[0]), 1, first.args[1])))
    cases.append(("duplicate enum tag", mutate_arg(
        text, "EnumValue", "DefinitionBodyDisposition", 2, "0x01", "ExecutableBody")))
    cases.append(("field ordinal gap", mutate_arg(
        text, "Field", "CanonicalCompilationRootRecord", 1, "9", "targetKind")))
    cases.append(("unknown bound", mutate_arg(
        text, "Record", "CanonicalCompilationRootRecord", 2, "UnknownBound")))
    cases.append(("unknown mutation", mutate_arg(
        text, "Record", "StableDefinitionQueryKey", 8, '"domain|unknown-mutation"')))
    cases.append(("stable header provenance", mutate_arg(
        text, "Record", "StableDefinitionHeader", 4, "CanonicalHeaderVerifier")))
    cases.append(("complete context producer identity", mutate_arg(
        text, "Record", "CompleteCompilationContextAuthority", 3,
        "VerifiedCoreDistributionInputTransaction")))
    cases.append(("complete context provider ownership", mutate_arg(
        text, "Input", "CompleteCompilationContextAuthorityInput", 6, "I1A")))

    sum_row = find_row(text, "Sum", "StableHeaderSite")
    cases.append(("missing sum owner", text[:sum_row.start] + text[sum_row.end:]))
    cases.append(("duplicate sum owner", text + "\n" + text[sum_row.start:sum_row.end] + "\n"))
    runtime = find_row(text, "RuntimeSum", "CapabilityDemandResult")
    runtime_args = list(runtime.args)
    runtime_args.insert(2, "S3")
    cases.append(("runtime sum codec", replace_row(text, runtime, runtime_args)))
    cases.append(("missing artifact task", mutate_arg(
        text, "Record", "StableDefinitionQueryKey", 5, "")))
    cases.append(("contradictory artifact task", mutate_arg(
        text, "Record", "StableDefinitionQueryKey", 5, "S2B")))
    cases.append(("missing executable test", mutate_arg(
        text, "Record", "StableDefinitionQueryKey", 9, '""')))
    cases.append(("capability result drift", mutate_arg(
        text, "CapabilityQuery", "MaterializeModuleGraph", 3,
        "CapabilityDemandResult<MaterializedModuleGraph>")))
    cases.append(("capability payload drift", mutate_arg(
        text, "CapabilityQuery", "MaterializeModuleGraph", 4, "MaterializedModuleSkeleton")))
    cases.append(("capability failure removed", mutate_arg(
        text, "CapabilityQuery", "MaterializeModuleGraph", 7,
        "SourceRejection<DiagnosticFact>")))
    cases.append(("capability failure added", mutate_arg(
        text, "CapabilityQuery", "MaterializeModuleGraph", 7,
        "SourceRejection<DiagnosticFact>|KeyRejection<BinderKeyFailure>|OtherFailure<Unit>")))
    cases.append(("capability failures exchanged", mutate_arg(
        text, "CapabilityQuery", "MaterializeModuleGraph", 7,
        "KeyRejection<BinderKeyFailure>|SourceRejection<DiagnosticFact>")))
    cases.append(("capability source payload drift", mutate_arg(
        text, "CapabilityQuery", "MaterializeModuleGraph", 7,
        "SourceRejection<DiagnosticRecord>|KeyRejection<BinderKeyFailure>")))
    cases.append(("capability key payload drift", mutate_arg(
        text, "CapabilityQuery", "MaterializeModuleGraph", 7,
        "SourceRejection<DiagnosticFact>|KeyRejection<OtherKeyFailure>")))
    for value in ("0", "1", "4294967294", "UINT64_MAX"):
        cases.append((f"package bound drift {value}", mutate_arg(
            text, "Bound", "CanonicalPackageRecordBytes", 1, value)))
    cases.append(("canonical input sequence bound drift", mutate_arg(
        text, "Bound", "CanonicalInputSequenceRecords", 1, "4294967294")))
    cases.append(("canonical input sequence rule drift", mutate_arg(
        text, "Bound", "CanonicalInputSequenceRecords", 2, '"CompleteRecordBytes"')))
    cases.append(("canonical input value bound drift", mutate_arg(
        text, "Bound", "CanonicalInputValueBytes", 1, "UINT64_MAX")))
    cases.append(("canonical input value rule drift", mutate_arg(
        text, "Bound", "CanonicalInputValueBytes", 2, '"CompleteRecordBytes"')))
    cases.append(("visibility wrapper drift", mutate_arg(
        text, "Query", "BindingVisibility", 3, "BindingVisibilityResult")))
    cases.append(("local export visibility drift", mutate_arg(
        text, "Field", "StableLocalExportFact", 3, "MemberVisibility", "visibility")))
    declaration = (
        "#define ZOM_STABLE_BINDING_RUNTIME_SUM_VARIANT("
        "sumName, variantName, tag, condition)"
    )
    if declaration not in text:
        return ["missing runtime-sum-variant declaration self-test fixture"]
    cases.append((
        "runtime-sum-variant declaration shape",
        text.replace(
            declaration,
            "#define ZOM_STABLE_BINDING_RUNTIME_SUM_VARIANT(sumName, variantName, tag)",
            1,
        ),
    ))

    swapped = mutate_arg(
        text,
        "EnumValue",
        "IdentityDiagnosticEmitter",
        2,
        "0x04",
        "ConstantExpressionNotAllowed",
    )
    swapped = mutate_arg(
        swapped,
        "EnumValue",
        "IdentityDiagnosticEmitter",
        2,
        "0x03",
        "DuplicateGenericParameter",
    )
    cases.append(("identity emitter tag exchange", swapped))
    mapping_mutations = (
        (1, "ZOM9999", "diagnostic code"),
        (2, "OtherArguments", "diagnostic arguments"),
        (3, "ZOM9998", "diagnostic secondary code"),
        (4, "OtherRole", "diagnostic secondary role"),
        (5, "2", "diagnostic secondary count"),
        (6, "1", "diagnostic fix-it count"),
    )
    for mapping in ("ConstantExpressionNotAllowed", "DuplicateGenericParameter"):
        for column, value, label in mapping_mutations:
            cases.append(
                (
                    f"{mapping} {label}",
                    mutate_arg(text, "DiagnosticMapping", mapping, column, value),
                )
            )

    required_error_fragments = {
        "capability failure removed": "schema-derived runtime substitutions",
        "capability failure added": "schema-derived runtime substitutions",
        "capability failures exchanged": "schema-derived runtime substitutions",
        "capability source payload drift": "schema-derived runtime substitutions",
        "capability key payload drift": "schema-derived runtime substitutions",
        "local export visibility drift": (
            "StableLocalExportFact.visibility must be Optional<MemberVisibility>"
        ),
        "runtime-sum-variant declaration shape": "macro RUNTIME_SUM_VARIANT",
        "complete context producer identity": (
            "producer must be CompleteCompilationContextAuthority::fromVerified"
        ),
        "complete context provider ownership": (
            "Input CompleteCompilationContextAuthorityInput: task columns"
        ),
    }
    failures: list[str] = []
    for name, mutated in cases:
        mutation_errors = validate(mutated)
        if not mutation_errors:
            failures.append(name)
            continue
        required = required_error_fragments.get(name)
        if required and not any(required in error for error in mutation_errors):
            failures.append(f"{name} (missing targeted rejection)")
    if failures:
        return [f"self-test mutation was accepted: {name}" for name in failures]

    inputs = read_repository_inputs()
    wiring_cases: list[tuple[str, dict[str, str]]] = []

    def with_input(name: str, value: str) -> dict[str, str]:
        mutated = dict(inputs)
        mutated[name] = value
        return mutated

    wiring_cases.append((
        "missing schema consumer",
        with_input(
            "facts-source",
            inputs["facts-source"].replace(
                '#include "zomlang/compiler/binder/stable-binding-schema.def"', "", 1
            ),
        ),
    ))
    wiring_cases.append((
        "missing facts source wiring",
        with_input(
            "binder-cmake",
            inputs["binder-cmake"].replace(
                "  ${CMAKE_CURRENT_SOURCE_DIR}/stable-binding-facts.cc\n", "", 1
            ),
        ),
    ))
    wiring_cases.append((
        "missing codec source wiring",
        with_input(
            "binder-cmake",
            inputs["binder-cmake"].replace(
                "  ${CMAKE_CURRENT_SOURCE_DIR}/stable-binding-codec.cc\n", "", 1
            ),
        ),
    ))
    wiring_cases.append((
        "missing ztest registration",
        with_input(
            "test-cmake",
            inputs["test-cmake"].replace(
                'add_ztest_unit_test("stable-binding-facts-test"', 'add_ztest_unit_test("removed"',
                1,
            ),
        ),
    ))
    wiring_cases.append((
        "missing schema CTest registration",
        with_input(
            "test-cmake",
            inputs["test-cmake"].replace(
                "add_test(NAME stable-binding-schema\n", "add_test(NAME removed-schema\n", 1
            ),
        ),
    ))
    missing_codec = dict(inputs)
    missing_codec["codec-header"] = inputs["codec-header"].replace(
        "OwnerAllocationRange", "RemovedCodecType"
    )
    missing_codec["codec-source"] = inputs["codec-source"].replace(
        "OwnerAllocationRange", "RemovedCodecType"
    )
    wiring_cases.append(("missing S3 codec", missing_codec))
    wiring_cases.append((
        "missing S3 wire oracle",
        with_input(
            "native-test",
            inputs["native-test"].replace("OwnerAllocationRange", "RemovedTestType"),
        ),
    ))

    wiring_failures = [
        name
        for name, mutated_inputs in wiring_cases
        if not validate_repository_wiring(text, mutated_inputs)
    ]
    if wiring_failures:
        return [
            f"repository wiring self-test mutation was accepted: {name}"
            for name in wiring_failures
        ]
    print(
        "stable binding schema self-test: PASS "
        f"({len(cases)} schema mutations, {len(wiring_cases)} wiring mutations)"
    )
    return []


def main() -> int:
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true", help="validate the live schema")
    mode.add_argument(
        "--self-test", action="store_true", help="run in-memory adversarial mutations"
    )
    args = parser.parse_args()
    text = SCHEMA.read_text(encoding="utf-8")
    errors = validate_live_repository(text) if args.check else self_test(text)
    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1
    if args.check:
        print("stable binding schema: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
