#!/usr/bin/env python3
"""Enforce source-backed core distribution and installed-consumer wiring."""

from __future__ import annotations

import argparse
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CORE = Path("products/zomcore")
TESTS = Path("products/zomlang/tests")
CORE_QUERY = Path("products/zomlang/compiler/driver/core/query.cc")
CORE_QUERY_HEADER = Path("products/zomlang/compiler/driver/core/query.h")
CORE_VERIFIER = Path("products/zomlang/compiler/driver/core/verifier.cc")
CORE_SIGNATURE = Path("products/zomlang/compiler/driver/core/signature.cc")
CORE_AUTHORITY = Path("products/zomlang/compiler/driver/core/marker-authority.cc")
CORE_LIBRARY = Path("products/zomlang/compiler/driver/core/library.cc")
CORE_LIBRARY_HEADER = Path("products/zomlang/compiler/driver/core/library.h")
INTERFACE_SOURCE = Path("products/zomlang/compiler/driver/interface/interface-source.h")
IMPORT_PROJECTOR = Path("products/zomlang/compiler/driver/interface/imported-signature-view-projector.h")
BORROW_EVIDENCE = Path("products/zomlang/compiler/driver/interface/borrow-evidence.h")
CHECKED_MODULE = Path("products/zomlang/compiler/hir/checked-module.h")
CORE_INVENTORY_TEST = Path(
    "products/zomlang/tests/unittests/compiler/driver/core/core-library-inventory-test.cc"
)
CORE_FINAL_INTERFACE_TEST = Path(
    "products/zomlang/tests/unittests/compiler/driver/session/compiler-session-test.cc"
)
CORE_REEXPORT_TEST = Path(
    "products/zomlang/tests/unittests/compiler/driver/session/compiler-session-package-test.cc"
)
CORE_INSTALLED_CONSUMER = Path(
    "products/zomlang/tests/integration/core-library/installed-consumer/src/main.zom"
)
COMPILER = Path("products/zomlang/compiler")
BOOTSTRAP_PRIVATE_FILES = frozenset(
    {
        Path("products/zomlang/compiler/driver/core/query.h"),
        Path("products/zomlang/compiler/driver/core/query.cc"),
        Path("products/zomlang/compiler/driver/core/verifier.h"),
        Path("products/zomlang/compiler/driver/core/verifier.cc"),
        Path("products/zomlang/compiler/driver/core/signature.h"),
        Path("products/zomlang/compiler/driver/core/signature.cc"),
        Path("products/zomlang/compiler/query/query-descriptor-schema.def"),
    }
)
BOOTSTRAP_IDENTIFIERS = (
    "CoreBootstrapModuleInterfaceRecord",
    "VerifiedCoreBootstrapModuleInterface",
    "MaterializeCoreBootstrapModuleInterfaceQuery",
)
REQUIRED = (
    CORE / "src/core.zom",
    CORE / "src/core/marker.zom",
    CORE / "src/core/prelude.zom",
    CORE / "CMakeLists.txt",
    TESTS / "cmake/verify-core-source-install.cmake",
    TESTS / "cmake/verify-core-library-install-consumer.cmake",
    TESTS / "integration/core-library/installed-consumer/Zom.toml",
    CORE_INSTALLED_CONSUMER,
    CORE_INVENTORY_TEST,
    CORE_FINAL_INTERFACE_TEST,
    CORE_REEXPORT_TEST,
)


def files() -> dict[Path, str]:
    compiler_sources = tuple(
        path.relative_to(ROOT)
        for suffix in ("*.h", "*.cc", "*.def")
        for path in (ROOT / COMPILER).rglob(suffix)
    )
    return {
        path: (ROOT / path).read_text(encoding="utf-8")
        for path in (
            *REQUIRED,
            CORE_QUERY,
            CORE_QUERY_HEADER,
            CORE_VERIFIER,
            CORE_SIGNATURE,
            CORE_AUTHORITY,
            CORE_LIBRARY,
            CORE_LIBRARY_HEADER,
            INTERFACE_SOURCE,
            IMPORT_PROJECTOR,
            BORROW_EVIDENCE,
            CHECKED_MODULE,
            *compiler_sources,
        )
        if (ROOT / path).is_file()
    }


def check(values: dict[Path, str]) -> list[str]:
    errors: list[str] = []
    for path in REQUIRED:
        if path not in values:
            errors.append(f"missing core-library artifact: {path}")
    if CORE_QUERY not in values:
        errors.append(f"missing core role-seed provider: {CORE_QUERY}")
    if CORE_QUERY_HEADER not in values:
        errors.append(f"missing core role-seed API: {CORE_QUERY_HEADER}")
    if CORE_VERIFIER not in values:
        errors.append(f"missing core bootstrap-interface verifier: {CORE_VERIFIER}")
    if CORE_SIGNATURE not in values:
        errors.append(f"missing core signature closure: {CORE_SIGNATURE}")
    if CORE_AUTHORITY not in values:
        errors.append(f"missing core marker authority facts: {CORE_AUTHORITY}")
    if CORE_LIBRARY not in values:
        errors.append(f"missing final core library publication: {CORE_LIBRARY}")
    if CORE_LIBRARY_HEADER not in values:
        errors.append(f"missing final core library API: {CORE_LIBRARY_HEADER}")
    if INTERFACE_SOURCE not in values:
        errors.append(f"missing verified interface source algebra: {INTERFACE_SOURCE}")
    if IMPORT_PROJECTOR not in values:
        errors.append(f"missing imported interface projector: {IMPORT_PROJECTOR}")
    if BORROW_EVIDENCE not in values:
        errors.append(f"missing borrow-evidence interface consumer: {BORROW_EVIDENCE}")
    if CHECKED_MODULE not in values:
        errors.append(f"missing checked-module interface consumer: {CHECKED_MODULE}")
    inventory_test = values.get(CORE_INVENTORY_TEST, "")
    for marker in (
        "initialCoreDistributionInput()",
        "computeCoreDistributionDigest(record)",
        '"core.zom"_zc',
        '"marker.zom"_zc',
        '"prelude.zom"_zc',
        "CoreSemanticRole::Copy",
        "CoreSemanticRole::Linear",
    ):
        if marker not in inventory_test:
            errors.append(f"{CORE_INVENTORY_TEST}: missing embedded inventory oracle: {marker}")
    final_interface_test = values.get(CORE_FINAL_INTERFACE_TEST, "")
    for marker in (
        "foundRoot",
        "foundMarker",
        "foundPrelude",
        "record.definedRoles()[0].role == source::core::CoreSemanticRole::Copy",
        "record.definedRoles()[1].role == source::core::CoreSemanticRole::Linear",
        "record.lookupDefinitions()[index].definition() ==",
    ):
        if marker not in final_interface_test:
            errors.append(
                f"{CORE_FINAL_INTERFACE_TEST}: missing final core interface oracle: {marker}"
            )
    reexport_test = values.get(CORE_REEXPORT_TEST, "")
    for marker in (
        "CompilerSession projects core prelude re-exports through the prelude surface",
        "ToolchainCoreImportedBindingSurfaceRevision",
        "root.sourceModule != prelude.sourceModule()",
    ):
        if marker not in reexport_test:
            errors.append(f"{CORE_REEXPORT_TEST}: missing core prelude re-export oracle: {marker}")
    installed_consumer = values.get(CORE_INSTALLED_CONSUMER, "")
    if "import core::prelude::{Copy, Linear};" not in installed_consumer:
        errors.append(f"{CORE_INSTALLED_CONSUMER}: missing explicit core prelude import")
    for path, text in values.items():
        if path not in BOOTSTRAP_PRIVATE_FILES and any(
            identifier in text for identifier in BOOTSTRAP_IDENTIFIERS
        ):
            errors.append(f"{path}: bootstrap-only core interface escapes finalization")
    core_cmake = values.get(CORE / "CMakeLists.txt", "")
    for source in ("src/core.zom", "src/core/marker.zom", "src/core/prelude.zom"):
        if source not in core_cmake:
            errors.append(f"{CORE / 'CMakeLists.txt'}: missing core source: {source}")
    test_cmake = (ROOT / TESTS / "CMakeLists.txt").read_text(encoding="utf-8")
    for marker in ("core-source-install-layout", "core-library-install-consumer"):
        if marker not in test_cmake:
            errors.append(f"{TESTS / 'CMakeLists.txt'}: missing installation gate: {marker}")
    session = (ROOT / "products/zomlang/compiler/driver/session/compiler-session.cc").read_text(
        encoding="utf-8"
    )
    if "installVerifiedCoreDistribution" not in session:
        errors.append("CompilerSession: missing installed core-distribution admission")
    provider = values.get(CORE_QUERY, "")
    materializer_visibility = (
        "!roleDefinitionIsPublic(bound.lease().capability(), selected.definition)"
    )
    if materializer_visibility not in provider:
        errors.append("Core role-seed materializer: missing retained visibility gate")
    verifier_visibility = (
        "!materializedRolesMatchBound(bound.lease().capability(), candidate.roles())"
    )
    if verifier_visibility not in provider:
        errors.append("Core role-seed verifier: missing retained visibility gate")
    bootstrap_verifier_call = "CoreLibraryQueryVerifier::verifyBootstrapModuleInterface"
    if bootstrap_verifier_call not in provider:
        errors.append("Core bootstrap-interface provider is missing")
    bootstrap_record_query = "CoreBootstrapModuleInterfaceQuery::provide"
    if bootstrap_record_query not in provider:
        errors.append("Core bootstrap-interface stable record query is missing")
    export_surface_query = "CoreExportSurfaceQuery::provide"
    if export_surface_query not in provider:
        errors.append("Core export-surface stable record query is missing")
    prelude_surface_query = "CorePreludeSurfaceQuery::provide"
    if prelude_surface_query not in provider:
        errors.append("Core prelude-surface stable record query is missing")
    verifier = values.get(CORE_VERIFIER, "")
    if "verifyBootstrapModuleInterfaceRecord" not in verifier:
        errors.append("Core bootstrap-interface stable record verifier is missing")
    if "verifyExportSurface" not in verifier:
        errors.append("Core export-surface independent verifier is missing")
    if "verifyPreludeSurface" not in verifier:
        errors.append("Core prelude-surface independent verifier is missing")
    if "verifyCoreAuthority" not in verifier:
        errors.append("Core authority independent verifier is missing")
    if "verifyFinalCoreModuleInterface" not in verifier:
        errors.append("Core final interface independent verifier is missing")
    if "verifyBootstrapModuleInterface" not in verifier:
        errors.append("Core bootstrap-interface independent verifier is missing")
    signature = values.get(CORE_SIGNATURE, "")
    if "matchesInitialSurface" not in signature:
        errors.append("Core signature closure is missing the initial-surface gate")
    if "VerifiedCoreImportedSignatureView" not in signature:
        errors.append("Core signature closure is missing the bootstrap imported-signature view")
    if "TypeFreeInterfaceSignatureRecord::decodeCanonical" not in signature:
        errors.append("Core signature closure is missing strict type-free signature decoding")
    for forbidden in ("ImportedSignatureModule", "SemanticTypeStore"):
        if forbidden in signature:
            errors.append(f"Core bootstrap signatures must not depend on {forbidden}")
    if "core::matchesInitialSurface" not in provider:
        errors.append("Core bootstrap provider bypasses the shared initial-surface gate")
    if "core::matchesInitialSurface" not in verifier:
        errors.append("Core bootstrap verifier bypasses the shared initial-surface gate")
    if "materializeCoreImportedSignatures" not in provider:
        errors.append("Core bootstrap provider is missing the imported-signature materialization")
    if "materializeCoreImportedSignatures" not in verifier:
        errors.append("Core bootstrap verifier is missing the imported-signature reconstruction")
    if "MaterializeCoreAuthorityQuery::provide" not in provider:
        errors.append("Core authority materializer is missing")
    if "FinalizeCoreModuleInterfaceQuery::provide" not in provider:
        errors.append("Core final interface materializer is missing")
    if "CoreModuleInterfaceRecord" not in provider:
        errors.append("Core final interface materializer is missing its flat record")
    final_binding_projection = "context.get<binder::ModuleExportNames>(module.clone())"
    if final_binding_projection not in provider:
        errors.append("Core final interface materializer is missing stable binding-name projection")
    if final_binding_projection not in verifier:
        errors.append("Core final interface verifier is missing stable binding-name projection")
    exported_binding_projection = "context.get<binder::ExportedBinding>("
    if exported_binding_projection not in provider:
        errors.append("Core final interface materializer is missing stable exported-binding projection")
    if exported_binding_projection not in verifier:
        errors.append("Core final interface verifier is missing stable exported-binding projection")
    final_signature_decoder = "TypeFreeInterfaceSignatureRecord::decodeCanonical"
    if final_signature_decoder not in provider:
        errors.append("Core final interface materializer is missing stable signature decoding")
    if final_signature_decoder not in verifier:
        errors.append("Core final interface verifier is missing stable signature decoding")
    final_root_projection = "finalSignatureRoots(signatureSource, signatureSurface,"
    if final_root_projection not in provider:
        errors.append("Core final interface materializer is missing signature-root projection")
    if final_root_projection not in verifier:
        errors.append("Core final interface verifier is missing signature-root reconstruction")
    final_record_start = provider.find("struct CoreModuleInterfaceRecord::Impl final")
    final_record_end = provider.find("struct VerifiedCoreModuleInterface::Impl final")
    if final_record_start < 0 or final_record_end <= final_record_start:
        errors.append("Core final interface record has no isolated implementation boundary")
    else:
        final_record = provider[final_record_start:final_record_end]
        for forbidden in (
            "CoreBootstrapModuleInterface",
            "CoreSignatureFact",
            "binder::ExportSurfaceRevision",
            "identity::DefId",
            "identity::ModuleId",
            "identity::SourceSpan",
        ):
            if forbidden in final_record:
                errors.append(
                    f"Core final interface record must remain handle-free: {forbidden}"
                )
    if "VerifiedCoreAuthorityBundle" not in provider:
        errors.append("Core authority materializer is missing its retained bundle")
    authority_header = values.get(CORE_QUERY_HEADER, "")
    if "PreludeBoundModuleLease" not in authority_header:
        errors.append("Core authority must retain the verified prelude bound-module lease")
    if "PreludeInterfaceLease" in authority_header:
        errors.append("Core authority must not retain the bootstrap interface lease")
    authority_start = authority_header.find("class VerifiedCoreAuthorityBundle final {")
    authority_end = authority_header.find("struct MaterializeCoreAuthorityQuery final {")
    if authority_start < 0 or authority_end <= authority_start:
        errors.append("Core authority has no isolated public/private API boundary")
    else:
        authority_surface = authority_header[authority_start:authority_end]
        private_start = authority_surface.find("private:")
        if private_start < 0:
            errors.append("Core authority must hide bootstrap retention leases")
        else:
            public_surface = authority_surface[:private_start]
            private_surface = authority_surface[private_start:]
            for member in (
                "VerifiedCoreAuthorityBundle> from(",
                "roleSeedLease()",
                "preludeBoundModuleLease()",
            ):
                if member in public_surface or member not in private_surface:
                    errors.append(
                        f"Core authority must keep bootstrap retention member private: {member}"
                    )
    authority_provider_start = provider.find("MaterializeCoreAuthorityQuery::provide")
    authority_provider_end = provider.find("FinalizeCoreModuleInterfaceQuery::provide")
    authority_verifier_start = verifier.find("verifyCoreAuthority")
    authority_verifier_end = verifier.find("verifyFinalCoreModuleInterface")
    if min(authority_provider_start, authority_provider_end, authority_verifier_start, authority_verifier_end) < 0:
        errors.append("Core authority has no isolated materialization and verification boundary")
    else:
        authority_provider = provider[authority_provider_start:authority_provider_end]
        authority_verifier = verifier[authority_verifier_start:authority_verifier_end]
        for boundary in (authority_provider, authority_verifier):
            if "VerifyBoundModuleQuery" not in boundary:
                errors.append("Core authority must demand the verified prelude bound module")
            if "MaterializeCoreBootstrapModuleInterfaceQuery" in boundary:
                errors.append("Core authority must not demand the bootstrap interface")
    if "policyTemplateIsCanonical" not in provider:
        errors.append("Core authority must authenticate the distribution policy template")
    for marker in ("shapes().encodeCanonical()", "policies().encodeCanonical()", "authority().encodeCanonical()"):
        if marker not in provider:
            errors.append(f"Core authority witness is missing {marker}")
    authority = values.get(CORE_AUTHORITY, "")
    for marker in (
        "VerifiedCoreMarkerShapeInventory",
        "VerifiedCoreMarkerPolicyRegistry",
        "VerifiedCoreStandardMarkerAuthority",
        "CoreResolvedMarkerPolicy",
        "zom.core-marker-shape-inventory",
        "zom.core-marker-policy-registry",
        "zom.standard-marker-authority",
    ):
        if marker not in authority:
            errors.append(f"Core marker authority facts are missing {marker}")
    library = values.get(CORE_LIBRARY, "")
    for marker in ("VerifiedCoreLibrary", "VerifiedCoreModule"):
        if marker not in library:
            errors.append(f"Core library publication is missing {marker}")
    if "CoreBootstrapModuleInterfaceRecord" in library:
        errors.append("Core library publication must not retain bootstrap interface records")
    library_header = values.get(CORE_LIBRARY_HEADER, "")
    if "VerifiedCoreModuleInterface" not in library_header:
        errors.append("Core library publication must retain final interface leases")
    for factory in ("VerifiedCoreModule", "VerifiedCoreLibrary"):
        factory_start = library_header.find(f"class {factory} final {{")
        factory_end = library_header.find("};", factory_start)
        factory_surface = library_header[factory_start:factory_end]
        private_start = factory_surface.find("private:")
        if factory_start < 0 or private_start < 0:
            errors.append(f"Core library {factory} factory must be session-private")
        elif "static zc::Maybe" in factory_surface[:private_start]:
            errors.append(f"Core library {factory} factory must not be public")
    if "materializeCoreLibrary" not in session:
        errors.append("CompilerSession is missing final core library assembly")
    if "FinalizeCoreModuleInterfaceQuery" not in session:
        errors.append("CompilerSession is missing final core interface demand")
    interface_source = values.get(INTERFACE_SOURCE, "")
    for marker in (
        "UserVerifiedInterfaceSource",
        "ToolchainCoreVerifiedInterfaceSource",
        "VerifiedInterfaceSource",
    ):
        if marker not in interface_source:
            errors.append(f"Verified interface source algebra is missing {marker}")
    projector = values.get(IMPORT_PROJECTOR, "")
    if "zc::ArrayPtr<const VerifiedInterfaceSource> dependencyInterfaces" not in projector:
        errors.append("Imported interface projector must consume VerifiedInterfaceSource")
    if "zc::ArrayPtr<const VerifiedModuleInterface> dependencyInterfaces" in projector:
        errors.append("Imported interface projector retains the user-only input path")
    borrow_evidence = values.get(BORROW_EVIDENCE, "")
    if "zc::ArrayPtr<const VerifiedInterfaceSource> availableInterfaces" not in borrow_evidence:
        errors.append("Borrow evidence must consume VerifiedInterfaceSource")
    if "zc::ArrayPtr<const VerifiedModuleInterface> availableInterfaces" in borrow_evidence:
        errors.append("Borrow evidence retains the user-only input path")
    checked_module = values.get(CHECKED_MODULE, "")
    if "zc::ArrayPtr<const driver::VerifiedInterfaceSource> availableModuleInterfaces" not in checked_module:
        errors.append("Checked module must consume VerifiedInterfaceSource")
    if "zc::ArrayPtr<const driver::VerifiedModuleInterface> availableModuleInterfaces" in checked_module:
        errors.append("Checked module retains the user-only input path")
    for forbidden in ("VerifiedMarkerShapeInventory", "VerifiedMarkerPolicyRegistry"):
        if forbidden in provider or forbidden in verifier:
            errors.append(f"Core authority must not depend on whole-session {forbidden}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    values = files()
    if args.self_test:
        mutated = dict(values)
        path = CORE / "CMakeLists.txt"
        mutated[path] = mutated.get(path, "").replace(
            "src/core/prelude.zom", "src/core/missing.zom"
        )
        if not check(mutated):
            print("core-library architecture self-test escaped")
            return 1
        mutated = dict(values)
        mutated.pop(CORE_INVENTORY_TEST, None)
        if not check(mutated):
            print("core-library inventory oracle file self-test escaped")
            return 1
        mutated = dict(values)
        mutated[CORE_INVENTORY_TEST] = mutated.get(CORE_INVENTORY_TEST, "").replace(
            "computeCoreDistributionDigest(record)", "removedCoreDistributionDigest(record)", 1
        )
        if not check(mutated):
            print("core-library inventory digest self-test escaped")
            return 1
        mutated = dict(values)
        mutated.pop(CORE_FINAL_INTERFACE_TEST, None)
        if not check(mutated):
            print("core-library final interface oracle file self-test escaped")
            return 1
        mutated = dict(values)
        mutated[CORE_FINAL_INTERFACE_TEST] = mutated.get(CORE_FINAL_INTERFACE_TEST, "").replace(
            "record.definedRoles()[1].role == source::core::CoreSemanticRole::Linear",
            "removedCoreRoleAssertion",
            1,
        )
        if not check(mutated):
            print("core-library final interface role self-test escaped")
            return 1
        mutated = dict(values)
        mutated.pop(CORE_REEXPORT_TEST, None)
        if not check(mutated):
            print("core-library prelude re-export oracle file self-test escaped")
            return 1
        mutated = dict(values)
        mutated[CORE_REEXPORT_TEST] = mutated.get(CORE_REEXPORT_TEST, "").replace(
            "root.sourceModule != prelude.sourceModule()",
            "root.sourceModule == prelude.sourceModule()",
            1,
        )
        if not check(mutated):
            print("core-library prelude owner-boundary self-test escaped")
            return 1
        mutated = dict(values)
        mutated[CORE_INSTALLED_CONSUMER] = mutated.get(CORE_INSTALLED_CONSUMER, "").replace(
            "import core::prelude::{Copy, Linear};",
            "import core::prelude::{Copy};",
            1,
        )
        if not check(mutated):
            print("core-library installed-consumer import self-test escaped")
            return 1
        for marker in (
            "!roleDefinitionIsPublic(bound.lease().capability(), selected.definition)",
            "!materializedRolesMatchBound(bound.lease().capability(), candidate.roles())",
            "CoreLibraryQueryVerifier::verifyBootstrapModuleInterface",
            "CoreBootstrapModuleInterfaceQuery::provide",
            "CoreExportSurfaceQuery::provide",
            "CorePreludeSurfaceQuery::provide",
            "MaterializeCoreAuthorityQuery::provide",
            "FinalizeCoreModuleInterfaceQuery::provide",
            "policyTemplateIsCanonical",
            "shapes().encodeCanonical()",
            "policies().encodeCanonical()",
            "authority().encodeCanonical()",
            "context.get<binder::ModuleExportNames>(module.clone())",
            "context.get<binder::ExportedBinding>(",
            "TypeFreeInterfaceSignatureRecord::decodeCanonical",
            "finalSignatureRoots(signatureSource, signatureSurface,",
        ):
            mutated = dict(values)
            mutated[CORE_QUERY] = mutated.get(CORE_QUERY, "").replace(marker, "")
            if not check(mutated):
                print("core-library architecture self-test escaped")
                return 1
        for path, marker in (
            (CORE_QUERY_HEADER, "PreludeBoundModuleLease"),
            (CORE_QUERY_HEADER, "VerifiedCoreAuthorityBundle> from("),
            (CORE_QUERY_HEADER, "roleSeedLease()"),
            (CORE_QUERY_HEADER, "preludeBoundModuleLease()"),
            (CORE_QUERY, "module_graph_query::VerifyBoundModuleQuery"),
            (CORE_VERIFIER, "module_graph_query::VerifyBoundModuleQuery"),
        ):
            mutated = dict(values)
            mutated[path] = mutated.get(path, "").replace(marker, "")
            if not check(mutated):
                print("core-library architecture self-test escaped")
                return 1
        mutated = dict(values)
        mutated[CORE_QUERY_HEADER] = mutated.get(CORE_QUERY_HEADER, "").replace(
            "PreludeBoundModuleLease", "PreludeInterfaceLease", 1
        )
        if not check(mutated):
            print("core-library architecture self-test escaped")
            return 1
        for path in (CORE_QUERY, CORE_VERIFIER):
            mutated = dict(values)
            mutated[path] = mutated.get(path, "").replace(
                "module_graph_query::VerifyBoundModuleQuery",
                "MaterializeCoreBootstrapModuleInterfaceQuery",
            )
            if not check(mutated):
                print("core-library architecture self-test escaped")
                return 1
        for marker in ("VerifiedCoreLibrary", "VerifiedCoreModule"):
            mutated = dict(values)
            mutated[CORE_LIBRARY] = mutated.get(CORE_LIBRARY, "").replace(marker, "")
            if not check(mutated):
                print("core-library architecture self-test escaped")
                return 1
        mutated = dict(values)
        mutated[CORE_LIBRARY_HEADER] = mutated.get(CORE_LIBRARY_HEADER, "").replace(
            "VerifiedCoreModuleInterface", ""
        )
        if not check(mutated):
            print("core-library architecture self-test escaped")
            return 1
        for path, marker in (
            (INTERFACE_SOURCE, "ToolchainCoreVerifiedInterfaceSource"),
            (IMPORT_PROJECTOR, "zc::ArrayPtr<const VerifiedInterfaceSource> dependencyInterfaces"),
            (BORROW_EVIDENCE, "zc::ArrayPtr<const VerifiedInterfaceSource> availableInterfaces"),
            (CHECKED_MODULE, "zc::ArrayPtr<const driver::VerifiedInterfaceSource> availableModuleInterfaces"),
        ):
            mutated = dict(values)
            mutated[path] = mutated.get(path, "").replace(marker, "")
            if not check(mutated):
                print("core-library architecture self-test escaped")
                return 1
        mutated = dict(values)
        mutated[INTERFACE_SOURCE] = mutated.get(INTERFACE_SOURCE, "").replace(
            "UserVerifiedInterfaceSource",
            "UserVerifiedInterfaceSource\nVerifiedCoreBootstrapModuleInterface",
            1,
        )
        if not check(mutated):
            print("core-library architecture self-test escaped")
            return 1
        for marker in (
            "VerifiedCoreMarkerShapeInventory",
            "VerifiedCoreMarkerPolicyRegistry",
            "VerifiedCoreStandardMarkerAuthority",
            "CoreResolvedMarkerPolicy",
        ):
            mutated = dict(values)
            mutated[CORE_AUTHORITY] = mutated.get(CORE_AUTHORITY, "").replace(marker, "")
            if not check(mutated):
                print("core-library architecture self-test escaped")
                return 1
        mutated = dict(values)
        mutated[CORE_VERIFIER] = mutated.get(CORE_VERIFIER, "").replace(
            "verifyBootstrapModuleInterfaceRecord", "removedBootstrapModuleInterfaceVerifier", 1
        )
        if not check(mutated):
            print("core-library architecture self-test escaped")
            return 1
        mutated = dict(values)
        mutated[CORE_VERIFIER] = mutated.get(CORE_VERIFIER, "").replace(
            "verifyPreludeSurface", "removedCorePreludeSurfaceVerifier", 1
        )
        if not check(mutated):
            print("core-library architecture self-test escaped")
            return 1
        mutated = dict(values)
        mutated[CORE_VERIFIER] = mutated.get(CORE_VERIFIER, "").replace(
            "verifyExportSurface", "removedCoreExportSurfaceVerifier", 1
        )
        if not check(mutated):
            print("core-library architecture self-test escaped")
            return 1
        mutated = dict(values)
        mutated[CORE_VERIFIER] = mutated.get(CORE_VERIFIER, "").replace(
            "verifyFinalCoreModuleInterface", "removedFinalCoreModuleInterfaceVerifier", 1
        )
        if not check(mutated):
            print("core-library architecture self-test escaped")
            return 1
        for path, marker in (
            (CORE_SIGNATURE, "matchesInitialSurface"),
            (CORE_SIGNATURE, "VerifiedCoreImportedSignatureView"),
            (CORE_SIGNATURE, "TypeFreeInterfaceSignatureRecord::decodeCanonical"),
            (CORE_QUERY, "core::matchesInitialSurface"),
            (CORE_VERIFIER, "core::matchesInitialSurface"),
            (CORE_QUERY, "materializeCoreImportedSignatures"),
            (CORE_VERIFIER, "materializeCoreImportedSignatures"),
        ):
            mutated = dict(values)
            mutated[path] = mutated.get(path, "").replace(marker, "")
            if not check(mutated):
                print("core-library architecture self-test escaped")
                return 1
        mutated = dict(values)
        mutated[CORE_QUERY] = mutated.get(CORE_QUERY, "").replace(
            "struct CoreModuleInterfaceRecord::Impl final",
            "struct CoreModuleInterfaceRecord::Impl final { CoreSignatureFact leaked;",
            1,
        )
        if not check(mutated):
            print("core-library architecture self-test escaped")
            return 1
        for marker in (
            "context.get<binder::ModuleExportNames>(module.clone())",
            "context.get<binder::ExportedBinding>(",
            "TypeFreeInterfaceSignatureRecord::decodeCanonical",
        ):
            mutated = dict(values)
            mutated[CORE_VERIFIER] = mutated.get(CORE_VERIFIER, "").replace(marker, "", 1)
            if not check(mutated):
                print("core-library architecture self-test escaped")
                return 1
        print("core-library architecture self-test passed")
        return 0
    errors = check(values)
    if errors:
        print("\n".join(errors))
        return 1
    print("core-library architecture check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
