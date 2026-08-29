#!/usr/bin/env python3

import argparse
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
COMPILER_ROOT = ROOT / "compiler"
DOCS_ROOT = ROOT / "docs"
IR_ROOT = Path("compiler/ir")
IRGEN_ROOT = Path("compiler/irgen")
MIR_ROOT = Path("compiler/mir")
TARGET_HEADER = IR_ROOT / "target-registry.h"
TARGET_SOURCE = IR_ROOT / "target-registry.cc"
FAILURE_HEADER = IR_ROOT / "ir-failure.h"
FAILURE_SOURCE = IR_ROOT / "ir-failure.cc"
DIAGNOSTIC_ADAPTER_HEADER = IR_ROOT / "ir-diagnostic-adapter.h"
DIAGNOSTIC_ADAPTER_SOURCE = IR_ROOT / "ir-diagnostic-adapter.cc"
IDENTITY_HEADER = IR_ROOT / "ir-identity.h"
IDENTITY_SOURCE = IR_ROOT / "ir-identity.cc"
IR_CMAKE = IR_ROOT / "CMakeLists.txt"
# RFC 0043 D3b: the snapshot capability, its token seam, and its test peer are a
# test-only internal surface. Only the implementation file may include it from
# the compiler tree; every other production translation unit is forbidden (the
# invoke-linker-test lives under tests/, outside this scan, so it is exempt).
INVOKE_LINKER_INTERNAL_HEADER = IR_ROOT / "invoke-linker-internal.h"
INVOKE_LINKER_INTERNAL_INCLUDE = 'compiler/ir/invoke-linker-internal.h'
INVOKE_LINKER_INTERNAL_ALLOWED = (IR_ROOT / "invoke-linker.cc",)
MIR_HEADER = MIR_ROOT / "built-mir.h"
MIR_SOURCE = MIR_ROOT / "built-mir.cc"
MIR_CMAKE = MIR_ROOT / "CMakeLists.txt"
COMPILER_CMAKE = Path("compiler/CMakeLists.txt")
SESSION_HEADER = Path("compiler/driver/session/compiler-session.h")
SESSION_SOURCE = Path("compiler/driver/session/compiler-session.cc")
CLI_SOURCE = Path("utils/zomc/zomc.cc")
COMPILER_OPTIONS = Path("compiler/basic/compiler-opts.h")
DIAGNOSTIC_DEFS = Path("compiler/diagnostics/defs/diagnostics-lowering.def")

REQUIRED_TARGET_MARKERS = (
    "namespace zomlang::compiler::ir",
    "class CanonicalTargetSpec final",
    "class VerifiedTargetSelection final",
    "class TargetRegistrySnapshot final",
)

REQUIRED_FAILURE_MARKERS = (
    "enum class IrFailureKind",
    "enum class IrFailurePhase",
    "class IrFailureOwner final",
    "class IrFailureSite final",
    "class IrFailureDetail final",
    "class IrVerificationFailure final",
    "class IrFailureFactory final",
    "class IrOperationResult final",
    "class FeatureBoundaryVerificationResult final",
    "SortedNonEmptyFailureSequence",
    "isLegalIrFailureShape",
)

REQUIRED_IDENTITY_MARKERS = (
    "class InstanceId final",
    "class MirBlockId final",
    "class LirBlockId final",
)

FORBIDDEN_FAILURE_API_PATTERNS = (
    r"\bLoweringFailure\b",
    r"\bRawLoweringError\b",
    r"\bzc::String(?:Ptr)?\b",
    r"\bstd::(?:basic_)?string(?:_view)?\b",
    r"\bconst\s+char\s*\*",
    r"\bdiagnostics::DiagID\b",
    r"compiler/diagnostics/",
    r"compiler/ast/",
    r"compiler/binder/",
    r"compiler/parser/",
    r"\bNodeId\b",
)

REQUIRED_BACKEND_DIAGNOSTICS = {
    "PanicUnwindUnsupported",
    "BinaryEmissionUnavailable",
    "IrOutputCreationFailed",
    "TargetCapabilityUnavailable",
    "RecursiveInstantiation",
    "InstantiationBudgetExceeded",
    "CheckedModuleInvariant",
    "HirInvariant",
    "BuiltMirInvariant",
    "OwnershipProofInvariant",
    "ExecutableMirInvariant",
    "LirInvariant",
    "BackendInvariant",
    "IrCanonicalCodecMismatch",
    "FeatureBoundaryInvariant",
}

REQUIRED_DIAGNOSTIC_DEFINITIONS = (
    'DIAG(6008, IrOutputCreationFailed, kError, "IR emission could not create its output stream", 0)',
    'DIAG(6009, TargetCapabilityUnavailable, kError, "The selected target does not support the required compiler operation", 0)',
    'DIAG(6010, RecursiveInstantiation, kError, "Generic instantiation is recursively expanding", 0)',
    'DIAG(6011, InstantiationBudgetExceeded, kError, "Generic instantiation exceeds the configured compiler limit", 0)',
    'DIAG(9942, CheckedModuleInvariant, kFatal, "Internal checked-module invariant violated ({0} occurrence(s))", 1)',
    'DIAG(9943, HirInvariant, kFatal, "Internal HIR invariant violated ({0} occurrence(s))", 1)',
    'DIAG(9944, BuiltMirInvariant, kFatal, "Internal Built MIR invariant violated ({0} occurrence(s))", 1)',
    'DIAG(9945, OwnershipProofInvariant, kFatal, "Internal ownership proof invariant violated ({0} occurrence(s))", 1)',
    'DIAG(9946, ExecutableMirInvariant, kFatal, "Internal executable MIR invariant violated ({0} occurrence(s))", 1)',
    'DIAG(9947, LirInvariant, kFatal, "Internal LIR invariant violated ({0} occurrence(s))", 1)',
    'DIAG(9948, BackendInvariant, kFatal, "Internal backend invariant violated ({0} occurrence(s))", 1)',
    'DIAG(9949, IrCanonicalCodecMismatch, kFatal, "Internal IR canonical encoding is invalid ({0} occurrence(s))", 1)',
    'DIAG(9955, FeatureBoundaryInvariant, kFatal, "Internal feature-boundary invariant violated ({0} occurrence(s))", 1)',
)

REQUIRED_DIAGNOSTIC_ADAPTER_MARKERS = (
    "class IrDiagnosticLocationResolver",
    "class IrDiagnosticGroup final",
    "irDiagnosticId(IrFailureKind kind",
    "groupIrCapabilityFailures",
    "groupIrInvariantFailures",
    "emitIrDiagnosticGroups",
    "emitIrIdentityInvariantFailures",
)

RETIRED_PROTOTYPE_MARKERS = (
    "OutputType::IR",
    "emitIR(",
    "IrSingleSourceRequired",
    "IrCheckedInputMissing",
    "IrDumpInvariantViolation",
)

FORBIDDEN_ALTERNATE_MIR_DOMAIN_MARKER = "zom.mir-revision."
FORBIDDEN_VERSIONED_MIR_SYMBOL = re.compile(
    r"\b(?:BuiltMir|MirRevision|computeBuilt)V[0-9]+\b"
)
FORBIDDEN_VERSIONED_MIR_TEXT = re.compile(
    r"\b(?:Built MIR v[0-9]+|MIR[- ]v[0-9]+|zom\.mir(?:-revision)?\.v[0-9]+)\b",
    re.IGNORECASE,
)

FORBIDDEN_TARGET_DEPENDENCIES = (
    "compiler/ast/",
    "compiler/binder/",
    "compiler/checker/",
    "compiler/parser/",
    "NodeId",
    "BindingMetadata",
)

REQUIRED_BUILT_MIR_MARKERS = (
    "class MirRevisionId final",
    "class MirRevisionCodec final",
    "class BuiltMirCandidate final",
    "class VerifiedBuiltMir final",
    "class BuiltMirBuilder final",
    "class BuiltMirVerifier final",
)

REQUIRED_SESSION_MIR_MARKERS = (
    "zc::Vector<ownership::OwnershipCheckedMir> ownershipCheckedMirModules",
    "getOwnershipCheckedMirModules",
    "mir::BuiltMirBuilder::build",
    "mir::BuiltMirVerifier::verify",
    "impl->ownershipCheckedMirModules = zc::mv(stagedOwnershipCheckedMir)",
)

FORBIDDEN_BUILT_MIR_DEPENDENCIES = (
    "compiler/irgen/",
    "compiler/ir/target-registry.h",
    "TargetDataLayout",
    "CanonicalTargetSpec",
    "VerifiedTargetSelection",
)

FORBIDDEN_BUILT_MIR_DUPLICATE_BORROW_MARKERS = (
    "MirBorrowRvalue",
    "MirRvalue::borrow",
    "MirRvalueKind::Borrow",
)


def relative(path: Path) -> Path:
    return path.relative_to(ROOT)


def load_files() -> dict[Path, str]:
    files: dict[Path, str] = {}
    for suffix in ("*.h", "*.cc"):
        for path in COMPILER_ROOT.rglob(suffix):
            files[relative(path)] = path.read_text(encoding="utf-8")
    for path in DOCS_ROOT.rglob("*.md"):
        files[relative(path)] = path.read_text(encoding="utf-8")
    files[Path("AGENTS.md")] = (ROOT / "AGENTS.md").read_text(encoding="utf-8")
    for path in (
        COMPILER_CMAKE,
        IR_CMAKE,
        MIR_CMAKE,
        CLI_SOURCE,
        COMPILER_OPTIONS,
        DIAGNOSTIC_DEFS,
    ):
        files[path] = (ROOT / path).read_text(encoding="utf-8")
    return files


def check_invoke_linker_internal_boundary(files: dict[Path, str], errors: list[str]) -> None:
    """The D3b snapshot internal header may be included only by its implementation
    file within the compiler tree. Any other compiler translation unit that
    includes it re-exposes the snapshot capability and token seam that the public
    surface deliberately hides, so it is rejected."""
    for path in sorted(files):
        if path.suffix not in {".h", ".cc"}:
            continue
        if path in INVOKE_LINKER_INTERNAL_ALLOWED or path == INVOKE_LINKER_INTERNAL_HEADER:
            continue
        if INVOKE_LINKER_INTERNAL_INCLUDE in files[path]:
            errors.append(
                f"{path}: including {INVOKE_LINKER_INTERNAL_INCLUDE} is forbidden; the D3b "
                "snapshot capability is a test-only internal surface (only "
                "compiler/ir/invoke-linker.cc may include it)"
            )


def check_removed_prototype(files: dict[Path, str], errors: list[str]) -> None:
    for path in sorted(files):
        if path == IRGEN_ROOT or IRGEN_ROOT in path.parents:
            errors.append(f"{path}: compiler/irgen direct-replacement target must not exist")
        if path.suffix not in {".h", ".cc"}:
            continue
        text = files[path]
        if "compiler/irgen/" in text or re.search(r"\birgen::", text):
            errors.append(f"{path}: compiler/irgen dependency is forbidden")


def check_canonical_mir_domain(files: dict[Path, str], errors: list[str]) -> None:
    for path, text in sorted(files.items()):
        if FORBIDDEN_ALTERNATE_MIR_DOMAIN_MARKER in text:
            errors.append(f"{path}: alternate MIR revision domain is forbidden")
        if FORBIDDEN_VERSIONED_MIR_SYMBOL.search(text) or FORBIDDEN_VERSIONED_MIR_TEXT.search(text):
            errors.append(f"{path}: versioned MIR symbol is forbidden")


def check_target_registry(files: dict[Path, str], errors: list[str]) -> None:
    header = files.get(TARGET_HEADER, "")
    source = files.get(TARGET_SOURCE, "")
    for marker in REQUIRED_TARGET_MARKERS:
        if marker not in header:
            errors.append(f"{TARGET_HEADER}: missing canonical target marker {marker}")
    if '#include "compiler/ir/target-registry.h"' not in source:
        errors.append(f"{TARGET_SOURCE}: target registry must include its canonical owner header")
    for marker in FORBIDDEN_TARGET_DEPENDENCIES:
        if marker in header or marker in source:
            errors.append(f"{IR_ROOT}: target registry depends on forbidden semantic marker {marker}")


def check_built_mir(files: dict[Path, str], errors: list[str]) -> None:
    header = files.get(MIR_HEADER, "")
    source = files.get(MIR_SOURCE, "")
    cmake = files.get(MIR_CMAKE, "")
    for marker in REQUIRED_BUILT_MIR_MARKERS:
        if marker not in header:
            errors.append(f"{MIR_HEADER}: missing direct Built MIR marker {marker}")
    if 'constexpr char domain[] = "zom.mir-revision"' not in source:
        errors.append(f"{MIR_SOURCE}: missing canonical MIR revision domain")
    if '#include "compiler/mir/built-mir.h"' not in source:
        errors.append(f"{MIR_SOURCE}: implementation must include its canonical owner header")
    if "built-mir.cc" not in cmake:
        errors.append(f"{MIR_CMAKE}: missing direct Built MIR implementation")
    for marker in FORBIDDEN_BUILT_MIR_DEPENDENCIES:
        if marker in header or marker in source:
            errors.append(f"{MIR_ROOT}: Built MIR depends on forbidden target/prototype marker {marker}")
    for marker in FORBIDDEN_BUILT_MIR_DUPLICATE_BORROW_MARKERS:
        if marker in header or marker in source:
            errors.append(
                f"{MIR_ROOT}: Built MIR duplicate borrow issuance marker is forbidden: {marker}"
            )
    session = files.get(SESSION_HEADER, "") + files.get(SESSION_SOURCE, "")
    for marker in REQUIRED_SESSION_MIR_MARKERS:
        if marker not in session:
            errors.append(f"{SESSION_SOURCE}: missing atomic Built MIR session marker {marker}")


def check_failure_contract(files: dict[Path, str], errors: list[str]) -> None:
    header = files.get(FAILURE_HEADER, "")
    source = files.get(FAILURE_SOURCE, "")
    identity = files.get(IDENTITY_HEADER, "")
    for marker in REQUIRED_FAILURE_MARKERS:
        if marker not in header:
            errors.append(f"{FAILURE_HEADER}: missing RFC 0010 failure marker {marker}")
    for marker in REQUIRED_IDENTITY_MARKERS:
        if marker not in identity:
            errors.append(f"{IDENTITY_HEADER}: missing canonical IR identity marker {marker}")
    if '#include "compiler/ir/ir-failure.h"' not in source:
        errors.append(f"{FAILURE_SOURCE}: failure implementation must include its owner header")
    if '#include "compiler/ir/ir-identity.h"' not in files.get(IDENTITY_SOURCE, ""):
        errors.append(f"{IDENTITY_SOURCE}: identity implementation must include its owner header")
    for path, text in ((FAILURE_HEADER, header), (FAILURE_SOURCE, source)):
        for pattern in FORBIDDEN_FAILURE_API_PATTERNS:
            if re.search(pattern, text):
                errors.append(f"{path}: forbidden raw failure API pattern {pattern}")

    cmake = files.get(IR_CMAKE, "")
    for source_name in (
        "ir-diagnostic-adapter.cc",
        "ir-failure.cc",
        "ir-identity.cc",
        "target-registry.cc",
    ):
        if source_name not in cmake:
            errors.append(f"{IR_CMAKE}: missing canonical IR source {source_name}")


def check_diagnostic_adapter(files: dict[Path, str], errors: list[str]) -> None:
    header = files.get(DIAGNOSTIC_ADAPTER_HEADER, "")
    source = files.get(DIAGNOSTIC_ADAPTER_SOURCE, "")
    for marker in REQUIRED_DIAGNOSTIC_ADAPTER_MARKERS:
        if marker not in header and marker not in source:
            errors.append(f"{DIAGNOSTIC_ADAPTER_HEADER}: missing exhaustive adapter marker {marker}")
    if '#include "compiler/ir/ir-diagnostic-adapter.h"' not in source:
        errors.append(
            f"{DIAGNOSTIC_ADAPTER_SOURCE}: implementation must include its canonical owner header"
        )
    for kind in (
        "InputRevisionMismatch",
        "MissingRequiredFact",
        "AdditionalFact",
        "InvalidFact",
        "InvalidControlFlow",
        "InvalidPlace",
        "InvalidOwnershipProof",
        "InvalidCleanup",
        "InvalidCoroutineState",
        "InvalidSsa",
        "MissingTargetLayout",
        "InvalidAbi",
        "UnresolvedDispatch",
        "UnsupportedTargetCapability",
        "BackendTranslationRejected",
        "RecursiveInstantiation",
        "InstantiationBudgetExceeded",
        "OutputCreationFailed",
        "CanonicalCodecMismatch",
    ):
        if f"case IrFailureKind::{kind}:" not in source:
            errors.append(f"{DIAGNOSTIC_ADAPTER_SOURCE}: missing failure-kind mapping {kind}")
    for phase in (
        "CheckedModuleAssembly",
        "HirConstruction",
        "HirVerification",
        "MirConstruction",
        "BuiltMirVerification",
        "OwnershipProofValidation",
        "CleanupElaboration",
        "CoroutineElaboration",
        "ExecutableMirVerification",
        "Monomorphization",
        "TargetSelection",
        "LirLowering",
        "LirVerification",
        "LlvmTranslation",
        "ObjectEmission",
        "FeatureBoundaryVerification",
    ):
        if f"case IrFailurePhase::{phase}:" not in source:
            errors.append(f"{DIAGNOSTIC_ADAPTER_SOURCE}: missing failure-phase mapping {phase}")
    codec = source.find("case IrFailureKind::CanonicalCodecMismatch:")
    phase_switch = source.find("switch (phase)")
    if codec < 0 or phase_switch < 0 or codec > phase_switch:
        errors.append(
            f"{DIAGNOSTIC_ADAPTER_SOURCE}: canonical codec mismatch must precede phase mapping"
        )
    if "identity::groupIdentityInvariants" not in source or "identity::emitIdentityDiagnosticGroups" not in source:
        errors.append(
            f"{DIAGNOSTIC_ADAPTER_SOURCE}: identity failures must reuse the RFC 0011 adapter"
        )


def check_wiring(files: dict[Path, str], errors: list[str]) -> None:
    cmake = files.get(COMPILER_CMAKE, "")
    for marker in ("add_subdirectory(ir)", "$<TARGET_OBJECTS:ir>"):
        if marker not in cmake:
            errors.append(f"{COMPILER_CMAKE}: missing canonical IR wiring marker {marker}")
    for marker in ("add_subdirectory(irgen)", "$<TARGET_OBJECTS:irgen>"):
        if marker in cmake:
            errors.append(f"{COMPILER_CMAKE}: removed irgen wiring marker is forbidden: {marker}")
    for marker in ("add_subdirectory(mir)", "$<TARGET_OBJECTS:mir>"):
        if marker not in cmake:
            errors.append(f"{COMPILER_CMAKE}: missing direct Built MIR wiring marker {marker}")


def check_cli_surface(files: dict[Path, str], errors: list[str]) -> None:
    cli = files.get(CLI_SOURCE, "")
    options = files.get(COMPILER_OPTIONS, "")
    for marker in RETIRED_PROTOTYPE_MARKERS:
        if marker in cli or marker in options:
            errors.append(f"{CLI_SOURCE}: retired IR prototype marker is forbidden: {marker}")
    if re.search(r'\btype\s*==\s*"ir"', cli) or "ast, dispatch, ir, binary" in cli:
        errors.append(f"{CLI_SOURCE}: non-producing --emit=ir surface is forbidden")


def check_diagnostics(files: dict[Path, str], errors: list[str]) -> None:
    definitions = files.get(DIAGNOSTIC_DEFS, "")
    normalized = re.sub(r"\s+", " ", definitions)
    registered = set(re.findall(r"DIAG\(\d+,\s*([A-Za-z_]\w*)", definitions))
    for name in sorted(REQUIRED_BACKEND_DIAGNOSTICS - registered):
        errors.append(f"{DIAGNOSTIC_DEFS}: missing required backend diagnostic {name}")
    for name in ("IrSingleSourceRequired", "IrCheckedInputMissing", "IrDumpInvariantViolation"):
        if name in registered:
            errors.append(f"{DIAGNOSTIC_DEFS}: retired prototype diagnostic is forbidden: {name}")
    for definition in REQUIRED_DIAGNOSTIC_DEFINITIONS:
        if definition not in normalized:
            errors.append(f"{DIAGNOSTIC_DEFS}: missing exact RFC 0010 diagnostic {definition}")


def analyze(files: dict[Path, str]) -> list[str]:
    errors: list[str] = []
    check_removed_prototype(files, errors)
    check_invoke_linker_internal_boundary(files, errors)
    check_canonical_mir_domain(files, errors)
    check_target_registry(files, errors)
    check_built_mir(files, errors)
    check_failure_contract(files, errors)
    check_diagnostic_adapter(files, errors)
    check_wiring(files, errors)
    check_cli_surface(files, errors)
    check_diagnostics(files, errors)
    return sorted(set(errors))


def run_check() -> int:
    errors = analyze(load_files())
    if errors:
        print("IR architecture check failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    print("IR architecture check passed (canonical target owner and no mixed prototype rail).")
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


def append_source(files: dict[Path, str], path: Path, source: str) -> None:
    files[path] = files.get(path, "") + source


def remove_once(files: dict[Path, str], path: Path, marker: str) -> None:
    files[path] = files[path].replace(marker, "", 1)


def run_self_test() -> int:
    baseline = load_files()
    errors = analyze(baseline)
    if errors:
        print("IR architecture self-test baseline failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    failures: list[str] = []
    failures += expect_rejection(
        baseline,
        "irgen source restored",
        lambda files: files.__setitem__(IRGEN_ROOT / "ir.cc", "void prototype();\n"),
        "direct-replacement target must not exist",
    )
    failures += expect_rejection(
        baseline,
        "alternate MIR revision domain added",
        lambda files: files.__setitem__(
            MIR_ROOT / "injected-alternate.cc",
            'const char* domain = "zom.mir-revision.alternate";\n',
        ),
        "alternate MIR revision domain is forbidden",
    )
    failures += expect_rejection(
        baseline,
        "versioned MIR documentation added",
        lambda files: append_source(
            files, Path("docs/design/architecture.md"), "\nBuilt MIR v" + "7\n"
        ),
        "versioned MIR symbol is forbidden",
    )
    failures += expect_rejection(
        baseline,
        "Built MIR verifier removed",
        lambda files: remove_once(files, MIR_HEADER, "class BuiltMirVerifier final"),
        "missing direct Built MIR marker",
    )
    failures += expect_rejection(
        baseline,
        "canonical MIR domain removed",
        lambda files: remove_once(files, MIR_SOURCE, 'constexpr char domain[] = "zom.mir-revision"'),
        "missing canonical MIR revision domain",
    )
    failures += expect_rejection(
        baseline,
        "Built MIR target dependency injected",
        lambda files: append_source(
            files, MIR_SOURCE, '\n#include "compiler/ir/target-registry.h"\n'
        ),
        "Built MIR depends on forbidden target/prototype marker",
    )
    failures += expect_rejection(
        baseline,
        "Built MIR duplicate borrow rvalue restored",
        lambda files: append_source(files, MIR_HEADER, "\nstruct MirBorrowRvalue;\n"),
        "Built MIR duplicate borrow issuance marker is forbidden",
    )
    failures += expect_rejection(
        baseline,
        "Built MIR session publication removed",
        lambda files: remove_once(
            files, SESSION_SOURCE, "impl->ownershipCheckedMirModules = zc::mv(stagedOwnershipCheckedMir)"
        ),
        "missing atomic Built MIR session marker",
    )
    failures += expect_rejection(
        baseline,
        "Built MIR CMake wiring removed",
        lambda files: remove_once(files, COMPILER_CMAKE, "$<TARGET_OBJECTS:mir>"),
        "missing direct Built MIR wiring marker",
    )
    failures += expect_rejection(
        baseline,
        "target owner removed",
        lambda files: remove_once(files, TARGET_HEADER, "class TargetRegistrySnapshot final"),
        "missing canonical target marker",
    )
    failures += expect_rejection(
        baseline,
        "irgen CMake restored",
        lambda files: append_source(files, COMPILER_CMAKE, "\nadd_subdirectory(irgen)\n"),
        "removed irgen wiring marker is forbidden",
    )
    failures += expect_rejection(
        baseline,
        "IR CLI restored",
        lambda files: append_source(files, CLI_SOURCE, '\nif (type == "ir") emitIR();\n'),
        "retired IR prototype marker is forbidden",
    )
    failures += expect_rejection(
        baseline,
        "irgen include restored",
        lambda files: append_source(
            files,
            Path("compiler/driver/injected.cc"),
            '#include "compiler/irgen/ir.h"\n',
        ),
        "compiler/irgen dependency is forbidden",
    )
    failures += expect_rejection(
        baseline,
        "prototype diagnostic restored",
        lambda files: append_source(
            files,
            DIAGNOSTIC_DEFS,
            '\nDIAG(9902, IrCheckedInputMissing, kFatal, "invalid", 0)\n',
        ),
        "retired prototype diagnostic is forbidden",
    )
    failures += expect_rejection(
        baseline,
        "target registry imports checker",
        lambda files: append_source(
            files,
            TARGET_SOURCE,
            '\n#include "compiler/checker/facts/signature-facts.h"\n',
        ),
        "target registry depends on forbidden semantic marker",
    )
    failures += expect_rejection(
        baseline,
        "failure kind removed",
        lambda files: remove_once(files, FAILURE_HEADER, "enum class IrFailureKind"),
        "missing RFC 0010 failure marker",
    )
    failures += expect_rejection(
        baseline,
        "raw lowering failure restored",
        lambda files: append_source(files, FAILURE_HEADER, "\nstruct LoweringFailure {};\n"),
        "forbidden raw failure API pattern",
    )
    failures += expect_rejection(
        baseline,
        "string failure payload restored",
        lambda files: append_source(files, FAILURE_HEADER, "\nzc::String failureMessage;\n"),
        "forbidden raw failure API pattern",
    )
    failures += expect_rejection(
        baseline,
        "AST node identity leaked into IR failures",
        lambda files: append_source(files, FAILURE_HEADER, "\nast::NodeId sourceIdentity;\n"),
        "forbidden raw failure API pattern",
    )
    failures += expect_rejection(
        baseline,
        "failure implementation unwired",
        lambda files: remove_once(files, IR_CMAKE, "ir-failure.cc"),
        "missing canonical IR source",
    )
    failures += expect_rejection(
        baseline,
        "diagnostic adapter mapping removed",
        lambda files: files.__setitem__(
            DIAGNOSTIC_ADAPTER_SOURCE,
            files[DIAGNOSTIC_ADAPTER_SOURCE].replace(
                "case IrFailureKind::CanonicalCodecMismatch:", ""
            ),
        ),
        "missing failure-kind mapping",
    )
    failures += expect_rejection(
        baseline,
        "exact diagnostic contract changed",
        lambda files: remove_once(
            files,
            DIAGNOSTIC_DEFS,
            "Internal IR canonical encoding is invalid ({0} occurrence(s))",
        ),
        "missing exact RFC 0010 diagnostic",
    )
    failures += expect_rejection(
        baseline,
        "invoke-linker internal header leaked into another IR translation unit",
        lambda files: files.__setitem__(
            IR_ROOT / "target-registry.cc",
            f'#include "{INVOKE_LINKER_INTERNAL_INCLUDE}"\n'
            + files[IR_ROOT / "target-registry.cc"],
        ),
        "is a test-only internal surface",
    )

    if failures:
        print("IR architecture self-test failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print("IR architecture negative fixtures passed (23/23).")
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
