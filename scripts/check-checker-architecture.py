#!/usr/bin/env python3

import argparse
import ast
import os
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
COMPILER_ROOT = ROOT / "zomlang" / "compiler"
CHECKER_ROOT = Path("zomlang/compiler/checker")
TYPE_ROOT = Path("zomlang/compiler/type")
VENDOR_ROOT = Path("zomlang/compiler/driver/package/vendor")
SIGNATURE_FACTS_HEADER = CHECKER_ROOT / "facts/signature-facts.h"
SIGNATURE_FACTS_SOURCE = CHECKER_ROOT / "facts/signature-facts.cc"
MARKER_PROOF_HEADER = CHECKER_ROOT / "body/marker-proof.h"
MARKER_PROOF_SOURCE = CHECKER_ROOT / "body/marker-proof.cc"
CROSS_MODULE_FACTS_HEADER = CHECKER_ROOT / "facts/cross-module-facts.h"
CROSS_MODULE_FACTS_SOURCE = CHECKER_ROOT / "facts/cross-module-facts.cc"
COHERENCE_FACTS_HEADER = CHECKER_ROOT / "facts/coherence-facts.h"
COHERENCE_FACTS_SOURCE = CHECKER_ROOT / "facts/coherence-facts.cc"
MODULE_INTERFACE_CONTRACT_SOURCE = CHECKER_ROOT / "module-interface-contract.cc"
MODULE_INTERFACE_HEADER = Path("zomlang/compiler/driver/interface/module-interface.h")
MODULE_INTERFACE_SOURCE = Path("zomlang/compiler/driver/interface/module-interface.cc")
CHECKED_MODULE_HEADER = Path("zomlang/compiler/hir/checked-module.h")
CHECKED_MODULE_SOURCE = Path("zomlang/compiler/hir/checked-module.cc")
HIR_MODULE_HEADER = Path("zomlang/compiler/hir/hir-module.h")
HIR_MODULE_SOURCE = Path("zomlang/compiler/hir/hir-module.cc")
BUILT_MIR_HEADER = Path("zomlang/compiler/mir/built-mir.h")
BUILT_MIR_SOURCE = Path("zomlang/compiler/mir/built-mir.cc")
OWNERSHIP_OVERLAY_SOURCE = Path(
    "zomlang/compiler/ownership/ownership-event-overlay.cc"
)
ORDINARY_CORE_INTERFACE_CONSUMERS = (
    MODULE_INTERFACE_HEADER,
    MODULE_INTERFACE_SOURCE,
    Path("zomlang/compiler/driver/interface/borrow-evidence.h"),
    Path("zomlang/compiler/driver/interface/borrow-evidence.cc"),
    CHECKED_MODULE_HEADER,
    CHECKED_MODULE_SOURCE,
    HIR_MODULE_HEADER,
    HIR_MODULE_SOURCE,
    BUILT_MIR_HEADER,
    BUILT_MIR_SOURCE,
    Path("zomlang/compiler/ownership/ownership-event-overlay.h"),
    OWNERSHIP_OVERLAY_SOURCE,
)
BOOTSTRAP_CORE_INTERFACE_TYPES = (
    "CoreBootstrapModuleInterface",
    "MaterializeCoreBootstrapModuleInterface",
    "CoreBootstrapModuleInterfaceRecord",
    "VerifiedCoreBootstrapModuleInterface",
)
COHERENCE_BUILDER_HEADER = Path("zomlang/compiler/driver/interface/coherence-builder.h")
COHERENCE_BUILDER_SOURCE = Path("zomlang/compiler/driver/interface/coherence-builder.cc")
OPERATOR_KIND_HEADER = CHECKER_ROOT / "operator-kind.h"
OPERATOR_KIND_SOURCE = CHECKER_ROOT / "operator-kind.cc"
INFERENCE_CONTEXT_HEADER = CHECKER_ROOT / "inference/inference-context.h"
INFERENCE_CONTEXT_SOURCE = CHECKER_ROOT / "inference/inference-context.cc"
INFERENCE_RECOVERY_HEADER = CHECKER_ROOT / "inference/inference-recovery-context.h"
CHECKER_CMAKE = CHECKER_ROOT / "CMakeLists.txt"
DRIVER_CMAKE = Path("zomlang/compiler/driver/CMakeLists.txt")
SEMANTIC_TYPE_KEY_HEADER = TYPE_ROOT / "semantic-type-key.h"
SEMANTIC_TYPE_KEY_SOURCE = TYPE_ROOT / "semantic-type-key.cc"
SEMANTIC_TYPE_STORE_HEADER = TYPE_ROOT / "semantic-type-store.h"
SEMANTIC_TYPE_STORE_SOURCE = TYPE_ROOT / "semantic-type-store.cc"
SESSION_SOURCE = Path("zomlang/compiler/driver/session/compiler-session.cc")
TEST_CMAKE = Path("zomlang/tests/conformance/CMakeLists.txt")
CHECKER_DIAGNOSTICS = Path("zomlang/compiler/diagnostics/defs/diagnostics-checker.def")
CHECKER_SOURCE_DIAGNOSTICS = CHECKER_ROOT / "checker-source-diagnostics.def"
CHECKED_FACTS_HEADER = CHECKER_ROOT / "inference/checked-facts.h"
CHECKED_FACTS_SOURCE = CHECKER_ROOT / "inference/checked-facts.cc"
BODY_CHECKER_SOURCE = CHECKER_ROOT / "body/body-checker.cc"
BODY_CHECKER_HEADER = CHECKER_ROOT / "body/body-checker.h"
SCALAR_LITERAL_FACTS_SOURCE = CHECKER_ROOT / "facts/scalar-literal-facts.cc"
CHECKER_DIAGNOSTIC_ADAPTER = CHECKER_ROOT / "diagnostics/checker-diagnostic-adapter.cc"
MARKER_PROOF_TEST = Path(
    "zomlang/tests/unittests/compiler/checker/body/marker-proof-test.cc"
)

PLACEHOLDER_DIAGNOSTIC_RENDERINGS = (
    "<definition>",
    "<integer>",
    "<patterns>",
)

TYPE_CATEGORY_PLACEHOLDER_RENDERINGS = (
    "primitive",
    "tuple",
    "object",
    "dynamic-array",
    "slice",
    "fixed-array",
    "function",
    "nominal",
    "type-parameter",
    "union",
    "intersection",
    "reference",
    "raw-pointer",
    "existential",
    "interface-bound",
    "interface-self",
    "<primitive>",
    "<tuple>",
    "<object>",
    "<dynamic-array>",
    "<slice>",
    "<fixed-array>",
    "<function>",
    "<nominal>",
    "<type-parameter>",
    "<union>",
    "<intersection>",
    "<reference>",
    "<raw-pointer>",
    "<existential>",
    "<interface-bound>",
    "<interface-self>",
)

REMOVED_CHECKER_FILES = {
    CHECKER_ROOT / name
    for name in (
        "borrow-model.cc",
        "borrow-model.h",
        "checker.cc",
        "checker.h",
        "decl-signature.cc",
        "decl-signature.h",
        "exhaustiveness.cc",
        "exhaustiveness.h",
        "query-cycle-detector.cc",
        "query-cycle-detector.h",
        "trait-resolver.cc",
        "trait-resolver.h",
        "type-expr-utils.cc",
        "type-expr-utils.h",
    )
}

CANONICAL_TYPE_HEADERS = {
    "semantic-type-data.h",
    "semantic-type-key.h",
    "semantic-type-store.h",
}

RAW_SIGNATURE_VERIFIER_TYPES = (
    "SignatureFactsCandidate",
    "SignatureFactsVerificationInput",
    "SignatureFactsVerificationResult",
    "SignatureFactsVerifier",
)

VERIFIED_SIGNATURE_FACTS_ALLOWED = {
    SIGNATURE_FACTS_HEADER,
    SIGNATURE_FACTS_SOURCE,
    CHECKER_ROOT / "body/body-checker.h",
    CHECKER_ROOT / "body/body-checker.cc",
    CHECKER_ROOT / "body/marker-proof.h",
    CHECKER_ROOT / "body/marker-proof.cc",
    Path("zomlang/compiler/driver/interface/module-interface.h"),
    Path("zomlang/compiler/driver/interface/module-interface.cc"),
    Path("zomlang/compiler/driver/interface/borrow-evidence.h"),
    Path("zomlang/compiler/driver/interface/borrow-evidence.cc"),
    Path("zomlang/compiler/hir/checked-module.h"),
    Path("zomlang/compiler/hir/checked-module.cc"),
    Path("zomlang/compiler/hir/hir-module.h"),
    Path("zomlang/compiler/hir/hir-module.cc"),
    Path("zomlang/compiler/mir/built-mir.h"),
    Path("zomlang/compiler/mir/built-mir.cc"),
    Path("zomlang/compiler/ownership/ownership-event-overlay.cc"),
    Path("zomlang/compiler/driver/session/compiler-session.h"),
    SESSION_SOURCE,
    MARKER_PROOF_TEST,
}

AD_HOC_CHECKER_TYPES = (
    "ProductionChecker",
    "VerifiedCheckerOutput",
)

TYPE_INCLUDE_PATTERN = re.compile(
    r'^\s*#\s*include\s+"zomlang/compiler/type/([^"/]+)"', re.MULTILINE
)
TYPE_TREE_IDENTIFIER_PATTERN = re.compile(r"\b(?:type::Type|TypeEnv)\b")
PUBLIC_SEMANTIC_TYPE_RESOLVER_PATTERN = re.compile(r"\bSemanticTypeKeyResolver\b")
RAW_TYPE_CANONICALIZER_PATTERN = re.compile(r"\bTypeCanonicalizer\b")
CANONICAL_TYPE_DATA_CONSTRUCTION_PATTERN = re.compile(
    r"\b(?:semantic::)?CanonicalTypeData\s*[({]"
)

PRIMITIVE_OPERATOR_TAGS = (
    ("UnaryPlus", 0x01), ("Neg", 0x02), ("LogicalNot", 0x03),
    ("BitNot", 0x04), ("Dereference", 0x05), ("BorrowShared", 0x06),
    ("BorrowMutable", 0x07), ("PreIncrement", 0x08), ("PreDecrement", 0x09),
    ("PostIncrement", 0x0A), ("PostDecrement", 0x0B), ("Add", 0x0C),
    ("Sub", 0x0D), ("Mul", 0x0E), ("Div", 0x0F), ("Rem", 0x10),
    ("Pow", 0x11), ("Shl", 0x12), ("Shr", 0x13), ("UShr", 0x14),
    ("BitAnd", 0x15), ("BitOr", 0x16), ("BitXor", 0x17),
    ("LogicalAnd", 0x18), ("LogicalOr", 0x19), ("Eq", 0x1A),
    ("Ne", 0x1B), ("StrictEq", 0x1C), ("StrictNe", 0x1D),
    ("Lt", 0x1E), ("Le", 0x1F), ("Gt", 0x20), ("Ge", 0x21),
    ("Index", 0x22), ("IndexMut", 0x23), ("Contains", 0x24),
    ("NullCoalesce", 0x25),
)

COMPOUND_OPERATOR_TAGS = (
    ("AddAssign", 0x01), ("SubAssign", 0x02), ("MulAssign", 0x03),
    ("DivAssign", 0x04), ("RemAssign", 0x05), ("PowAssign", 0x06),
    ("ShlAssign", 0x07), ("ShrAssign", 0x08), ("UShrAssign", 0x09),
    ("BitAndAssign", 0x0A), ("BitOrAssign", 0x0B), ("BitXorAssign", 0x0C),
    ("LogicalAndAssign", 0x0D), ("LogicalOrAssign", 0x0E),
    ("NullCoalesceAssign", 0x0F),
)

TYPE_KEY_PATTERN_TAGS = (
    ("Primitive", "PatternPrimitive", 0x01, "primitive"),
    ("Tuple", "PatternTuple", 0x02, "tuple"),
    ("Object", "PatternObject", 0x03, "object"),
    ("DynamicArray", "PatternDynamicArray", 0x04, "dynamicArray"),
    ("Slice", "PatternSlice", 0x05, "slice"),
    ("FixedArray", "PatternFixedArray", 0x06, "fixedArray"),
    ("Function", "PatternFunction", 0x07, "function"),
    ("Nominal", "PatternNominal", 0x08, "nominal"),
    ("TypeParameter", "PatternTypeParameter", 0x09, "typeParameter"),
    ("Union", "PatternUnion", 0x0A, "unionOf"),
    ("Intersection", "PatternIntersection", 0x0B, "intersection"),
    ("Reference", "PatternReference", 0x0C, "reference"),
    ("RawPointer", "PatternRawPointer", 0x0D, "rawPointer"),
    ("Existential", "PatternExistential", 0x0E, "existential"),
    ("InterfaceBound", "PatternInterfaceBound", 0x0F, "interfaceBound"),
    ("InterfaceSelf", "PatternInterfaceSelf", 0x10, "interfaceSelf"),
    ("Parameter", "PatternParameter", 0x11, "parameter"),
)

AST_OPERATOR_MAPPINGS = (
    ("UnaryOperatorKind", "Plus", "PrimitiveOperation", "UnaryPlus"),
    ("UnaryOperatorKind", "Minus", "PrimitiveOperation", "Neg"),
    ("UnaryOperatorKind", "LogicalNot", "PrimitiveOperation", "LogicalNot"),
    ("UnaryOperatorKind", "BitNot", "PrimitiveOperation", "BitNot"),
    ("UnaryOperatorKind", "Deref", "PrimitiveOperation", "Dereference"),
    ("UnaryOperatorKind", "Ref", "PrimitiveOperation", "BorrowShared"),
    ("UnaryOperatorKind", "RefMut", "PrimitiveOperation", "BorrowMutable"),
    ("UnaryOperatorKind", "PreIncrement", "PrimitiveOperation", "PreIncrement"),
    ("UnaryOperatorKind", "PreDecrement", "PrimitiveOperation", "PreDecrement"),
    ("PostfixOperatorKind", "Increment", "PrimitiveOperation", "PostIncrement"),
    ("PostfixOperatorKind", "Decrement", "PrimitiveOperation", "PostDecrement"),
    ("PostfixOperatorKind", "ErrorPropagate", "ErrorOperatorKind", "Propagate"),
    ("PostfixOperatorKind", "ErrorUnwrap", "ErrorOperatorKind", "ForcedUnwrap"),
    ("BinaryOperatorKind", "Add", "PrimitiveOperation", "Add"),
    ("BinaryOperatorKind", "Sub", "PrimitiveOperation", "Sub"),
    ("BinaryOperatorKind", "Mul", "PrimitiveOperation", "Mul"),
    ("BinaryOperatorKind", "Div", "PrimitiveOperation", "Div"),
    ("BinaryOperatorKind", "Mod", "PrimitiveOperation", "Rem"),
    ("BinaryOperatorKind", "Pow", "PrimitiveOperation", "Pow"),
    ("BinaryOperatorKind", "Shl", "PrimitiveOperation", "Shl"),
    ("BinaryOperatorKind", "Shr", "PrimitiveOperation", "Shr"),
    ("BinaryOperatorKind", "UShr", "PrimitiveOperation", "UShr"),
    ("BinaryOperatorKind", "BitAnd", "PrimitiveOperation", "BitAnd"),
    ("BinaryOperatorKind", "BitOr", "PrimitiveOperation", "BitOr"),
    ("BinaryOperatorKind", "BitXor", "PrimitiveOperation", "BitXor"),
    ("BinaryOperatorKind", "LogAnd", "PrimitiveOperation", "LogicalAnd"),
    ("BinaryOperatorKind", "LogOr", "PrimitiveOperation", "LogicalOr"),
    ("BinaryOperatorKind", "Eq", "PrimitiveOperation", "Eq"),
    ("BinaryOperatorKind", "Ne", "PrimitiveOperation", "Ne"),
    ("BinaryOperatorKind", "StrictEq", "PrimitiveOperation", "StrictEq"),
    ("BinaryOperatorKind", "StrictNe", "PrimitiveOperation", "StrictNe"),
    ("BinaryOperatorKind", "Lt", "PrimitiveOperation", "Lt"),
    ("BinaryOperatorKind", "Le", "PrimitiveOperation", "Le"),
    ("BinaryOperatorKind", "Gt", "PrimitiveOperation", "Gt"),
    ("BinaryOperatorKind", "Ge", "PrimitiveOperation", "Ge"),
    ("AssignmentOperatorKind", "AddAssign", "CompoundAssignmentOperation", "AddAssign"),
    ("AssignmentOperatorKind", "SubAssign", "CompoundAssignmentOperation", "SubAssign"),
    ("AssignmentOperatorKind", "MulAssign", "CompoundAssignmentOperation", "MulAssign"),
    ("AssignmentOperatorKind", "DivAssign", "CompoundAssignmentOperation", "DivAssign"),
    ("AssignmentOperatorKind", "ModAssign", "CompoundAssignmentOperation", "RemAssign"),
    ("AssignmentOperatorKind", "PowAssign", "CompoundAssignmentOperation", "PowAssign"),
    ("AssignmentOperatorKind", "ShlAssign", "CompoundAssignmentOperation", "ShlAssign"),
    ("AssignmentOperatorKind", "ShrAssign", "CompoundAssignmentOperation", "ShrAssign"),
    ("AssignmentOperatorKind", "UShrAssign", "CompoundAssignmentOperation", "UShrAssign"),
    ("AssignmentOperatorKind", "BitAndAssign", "CompoundAssignmentOperation", "BitAndAssign"),
    ("AssignmentOperatorKind", "BitOrAssign", "CompoundAssignmentOperation", "BitOrAssign"),
    ("AssignmentOperatorKind", "BitXorAssign", "CompoundAssignmentOperation", "BitXorAssign"),
    ("AssignmentOperatorKind", "LogicalAndAssign", "CompoundAssignmentOperation", "LogicalAndAssign"),
    ("AssignmentOperatorKind", "LogicalOrAssign", "CompoundAssignmentOperation", "LogicalOrAssign"),
    ("AssignmentOperatorKind", "NullCoalesceAssign", "CompoundAssignmentOperation", "NullCoalesceAssign"),
)

ACCEPTED_CHECKER_DIAGNOSTICS = (
    (4001, "DynGenericMethod", "kError", "Interface {0} has generic method {1} and cannot be used as dyn", 2),
    (4002, "DynSelfReturn", "kError", "Interface {0} has method {1} returning Self and cannot be used as dyn", 2),
    (4003, "DynMoveSelf", "kError", "Interface {0} has method {1} with move self receiver and cannot be used as dyn", 2),
    (4004, "DynUnassociatedType", "kError", "Dyn interface {0} requires associated type {1} to be bound", 2),
    (4005, "DynStaticMethod", "kError", "Interface {0} has a static method and cannot be used as dyn", 1),
    (4006, "DynGatNotAllowed", "kError", "Interface {0} has generic associated type {1} and cannot be used as dyn", 2),
    (4007, "DynUnsizedParameter", "kError", "Interface {0} has method {1} with unsized type {2} and cannot be used as dyn", 3),
    (4008, "DynSuperNotObjectSafe", "kError", "Interface {0} inherits object-unsafe interface {1} and cannot be used as dyn", 2),
    (4009, "TypeCheckerTypeMismatch", "kError", "Type mismatch: expected {0}, got {1}", 2),
    (4010, "CannotUnifyTypes", "kError", "Cannot unify {0} with {1} in {2}", 3),
    (4011, "InfiniteType", "kError", "Inference creates an infinite type involving {0}", 1),
    (4012, "CannotCallNonFunction", "kError", "Cannot call value of type {0}", 1),
    (4013, "CheckerInvalidCast", "kError", "Invalid cast from {0} to {1}", 2),
    (4014, "CannotInferTypeParameter", "kError", "Cannot infer type parameter {0}; provide explicit type arguments", 1),
    (4015, "CannotInferNullInitializer", "kError", "Cannot infer type from null initializer without annotation", 0),
    (4016, "ExplicitTypeArgumentCountMismatch", "kError", "Expected {0} explicit type arguments, got {1}", 2),
    (4017, "ConflictingImpl", "kError", "Conflicting implementations of {0} for type {1}", 2),
    (4018, "CheckerTraitNotImplemented", "kError", "Type {0} does not implement {1}", 2),
    (4019, "OperatorTraitSignatureMismatch", "kError", "Operator {0} implementation for {1} has an incompatible signature", 2),
    (4020, "NoAssociatedTypeProjection", "kError", "No associated type {0} is available for {1}", 2),
    (4021, "AmbiguousAssociatedTypeProjection", "kError", "Associated type {0} is ambiguous for {1}; use a qualified projection", 2),
    (4022, "CheckerNonExhaustiveMatch", "kError", "Non-exhaustive match; missing patterns: {0}", 1),
    (4023, "CheckerUnreachableMatchArm", "kWarning", "Unreachable match arm: pattern never matches", 0),
    (4024, "CannotMutateImmutableVariable", "kError", "Cannot mutate immutable definition {0}", 1),
    (4025, "ErrorPropagateOutsideRaises", "kError", "Error propagation produces {0}, which is not accepted by raises {1}", 2),
    (4026, "ErrorUnwrapNonUnion", "kError", "Forced unwrap requires an error union, got {0}", 1),
    (4028, "InvalidBinaryOperands", "kError", "Operator {0} is not defined for {1} and {2}", 3),
    (4029, "InvalidComparisonOperands", "kError", "Comparison {0} is not defined for {1} and {2}", 3),
    (4030, "CannotDereferenceType", "kError", "Cannot dereference value of type {0}", 1),
    (4031, "PostfixUpdateRequiresNumeric", "kError", "Postfix update requires a numeric operand, got {0}", 1),
    (4032, "ErrorPropagateNonUnion", "kError", "Error propagation requires an error union, got {0}", 1),
    (4033, "ErrorUnionEmpty", "kError", "Postfix error operator requires a non-empty union operand", 0),
    (4035, "ExplicitTypeArgumentsRequireGenericCallee", "kError", "Explicit type arguments require a generic callee", 0),
    (4036, "CallArgumentCountMismatch", "kError", "Expected {0} arguments, got {1}", 2),
    (4037, "MemberNotFound", "kError", "No member named {0} exists in type {1}", 2),
    (4038, "IndexRequiresInteger", "kError", "Array index must be an integer, got {0}", 1),
    (4039, "TupleIndexRequiresIntegerLiteral", "kError", "Tuple index must be an integer literal", 0),
    (4040, "TupleIndexOutOfBounds", "kError", "Tuple index is out of bounds", 0),
    (4041, "CannotIndexType", "kError", "Cannot index value of type {0}", 1),
    (4044, "InvalidDynUpcast", "kError", "Invalid dyn upcast from {0} to {1}", 2),
    (4045, "ConditionMustBeBool", "kError", "Condition must have type bool, got {0}", 1),
    (4046, "MissingReturnValue", "kError", "Missing return value of type {0}", 1),
    (4047, "AggregateLiteralTargetRequired", "kError", "Aggregate literal requires a struct, class, or structural object target, got {0}", 1),
    (4048, "UnknownStructField", "kError", "Unknown field {0} in aggregate literal", 1),
    (4049, "MissingStructField", "kError", "Missing required field {0} in aggregate literal", 1),
    (4050, "ArrayElementTypeMismatch", "kError", "Array element type mismatch: expected {0}, got {1}", 2),
    (4051, "MatchGuardMustBeBool", "kError", "Match guard must have type bool, got {0}", 1),
    (4052, "RecursiveTypeAliasCycle", "kError", "Recursive type alias cycle", 0),
    (4054, "OrphanImpl", "kError", "Cannot implement {0} for {1}: neither declaration is local", 2),
    (4055, "DynDuplicateAssociatedTypeBinding", "kError", "Duplicate associated type binding {0} in dyn interface {1}", 2),
    (4071, "PreviousImplHere", "kNote", "Previous implementation is here", 0),
    (4072, "ObjectSafetyCauseHere", "kNote", "Object-safety failure originates here", 0),
    (4073, "AssociatedTypeCandidateHere", "kNote", "Candidate associated type is declared here", 0),
    (4074, "PreviousAssociatedBindingHere", "kNote", "Previous associated type binding is here", 0),
    (4075, "AliasCycleMemberHere", "kNote", "Type alias cycle continues here", 0),
    (4076, "OperatorMethodDeclaredHere", "kNote", "Operator method is declared here", 0),
    (4077, "BodyLiteralOutOfRange", "kError", "Literal {0} is not representable as {1}", 2),
    (4078, "ConstantValueOutOfRange", "kError", "Constant value for {0} is outside the required range", 1),
    (4079, "ConstantExpressionNotAllowed", "kError", "Expression is not allowed in a constant value", 0),
    (4080, "ConstantDependencyCycle", "kError", "Constant definition {0} participates in a dependency cycle", 1),
    (4081, "ConstantArithmeticFailure", "kError", "Constant operation {0} is invalid for its operands", 1),
    (4088, "MarkerInterfaceRequiresBodylessImpl", "kError", "Marker interfaces require a bodyless marker implementation", 0),
    (4089, "BehaviorInterfaceRequiresImplBody", "kError", "Behavior interfaces require an impl body", 0),
    (4090, "GenericMarkerInterfaceNotAllowed", "kError", "Marker interfaces cannot declare generic parameters", 0),
    (4091, "PositiveMarkerImplRequiresUnsafe", "kError", "A positive marker implementation requires unsafe", 0),
    (4092, "ExplicitImplConflictsWithBuiltinMarker", "kError", "Builtin marker evidence cannot be replaced by an explicit implementation", 0),
)

REMOVED_CHECKER_DIAGNOSTIC_CODES = (4027, 4034, 4042, 4043, 4053)


def relative(path: Path) -> Path:
    return path.relative_to(ROOT)


def production_files() -> dict[Path, str]:
    files: dict[Path, str] = {}
    for directory, names, basenames in os.walk(COMPILER_ROOT):
        directory_path = Path(directory)
        if relative(directory_path) == VENDOR_ROOT:
            names.clear()
            continue
        for basename in basenames:
            if not basename.endswith((".h", ".cc")):
                continue
            path = directory_path / basename
            relative_path = relative(path)
            files[relative_path] = path.read_text(encoding="utf-8")
    files[CHECKER_CMAKE] = (ROOT / CHECKER_CMAKE).read_text(encoding="utf-8")
    files[DRIVER_CMAKE] = (ROOT / DRIVER_CMAKE).read_text(encoding="utf-8")
    files[TEST_CMAKE] = (ROOT / TEST_CMAKE).read_text(encoding="utf-8")
    files[CHECKER_DIAGNOSTICS] = (ROOT / CHECKER_DIAGNOSTICS).read_text(
        encoding="utf-8"
    )
    files[CHECKER_SOURCE_DIAGNOSTICS] = (
        ROOT / CHECKER_SOURCE_DIAGNOSTICS
    ).read_text(encoding="utf-8")
    files[MARKER_PROOF_TEST] = (ROOT / MARKER_PROOF_TEST).read_text(encoding="utf-8")
    return files


def strip_cpp_comments_and_literals(text: str) -> str:
    text = re.sub(r"//.*?$", "", text, flags=re.MULTILINE)
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    text = re.sub(r'"(?:\\.|[^"\\])*"', '""', text)
    text = re.sub(r"'(?:\\.|[^'\\])*'", "''", text)
    return text


def is_checker_source(path: Path) -> bool:
    return path.is_relative_to(CHECKER_ROOT) and path.suffix in {".h", ".cc"}


def is_public_type_header(path: Path) -> bool:
    return (
        path.is_relative_to(TYPE_ROOT)
        and path.suffix == ".h"
        and "internal" not in path.parts
    )


def is_store_owned_canonical_construction(path: Path, code: str, position: int) -> bool:
    if path == SEMANTIC_TYPE_STORE_SOURCE:
        return True
    if path != SEMANTIC_TYPE_KEY_SOURCE:
        return False

    signature = "SemanticTypeStore::canonicalizeClosed"
    search_from = 0
    while True:
        function = code.find(signature, search_from)
        if function < 0:
            return False
        opening = code.find("{", function)
        if opening < 0:
            return False
        depth = 0
        for index in range(opening, len(code)):
            if code[index] == "{":
                depth += 1
            elif code[index] == "}":
                depth -= 1
                if depth == 0:
                    if opening < position < index:
                        return True
                    search_from = index + 1
                    break
        else:
            return False


def check_removed_rail(files: dict[Path, str], errors: list[str]) -> None:
    for path in sorted(REMOVED_CHECKER_FILES & files.keys()):
        errors.append(f"{path}: removed Checker rail must not exist")

    for path, text in sorted(files.items()):
        if path.suffix not in {".h", ".cc"}:
            continue
        code = strip_cpp_comments_and_literals(text)
        if re.search(r"\binternType\s*\(", code):
            errors.append(f"{path}: internType compatibility entry is forbidden")
        for name in AD_HOC_CHECKER_TYPES:
            if re.search(rf"\b{re.escape(name)}\b", code):
                errors.append(f"{path}: ad hoc Checker capability {name} is forbidden")


def check_canonical_type_boundary(files: dict[Path, str], errors: list[str]) -> None:
    for path, text in sorted(files.items()):
        if not is_checker_source(path):
            continue
        for include in TYPE_INCLUDE_PATTERN.findall(text):
            if include not in CANONICAL_TYPE_HEADERS:
                errors.append(f"{path}: non-canonical Checker type dependency {include}")
        if TYPE_TREE_IDENTIFIER_PATTERN.search(strip_cpp_comments_and_literals(text)):
            errors.append(f"{path}: Type tree or TypeEnv dependency is forbidden in Checker")


def check_store_owned_type_admission(files: dict[Path, str], errors: list[str]) -> None:
    for path, text in sorted(files.items()):
        code = strip_cpp_comments_and_literals(text)
        if is_public_type_header(path) and PUBLIC_SEMANTIC_TYPE_RESOLVER_PATTERN.search(code):
            errors.append(
                f"{path}: public SemanticTypeKeyResolver capability is forbidden"
            )
        if RAW_TYPE_CANONICALIZER_PATTERN.search(code):
            errors.append(
                f"{path}: raw TypeCanonicalizer canonicalization entry is forbidden"
            )
        if path.suffix == ".cc":
            for construction in CANONICAL_TYPE_DATA_CONSTRUCTION_PATTERN.finditer(code):
                if not is_store_owned_canonical_construction(
                    path, code, construction.start()
                ):
                    errors.append(
                        f"{path}: CanonicalTypeData construction is forbidden outside "
                        "SemanticTypeStore"
                    )
                    break


def check_signature_prototype_boundary(files: dict[Path, str], errors: list[str]) -> None:
    allowed = {SIGNATURE_FACTS_HEADER, SIGNATURE_FACTS_SOURCE}
    for path, text in sorted(files.items()):
        if path in allowed or path.suffix not in {".h", ".cc"}:
            continue
        code = strip_cpp_comments_and_literals(text)
        for name in RAW_SIGNATURE_VERIFIER_TYPES:
            if re.search(rf"\b{re.escape(name)}\b", code):
                errors.append(
                    f"{path}: signature prototype capability {name} is forbidden outside "
                    "signature-facts"
                )
        if (
            path not in VERIFIED_SIGNATURE_FACTS_ALLOWED
            and re.search(r"\bVerifiedSignatureFacts\b", code)
        ):
            errors.append(
                f"{path}: verified signature capability is forbidden outside its "
                "module-interface, body-checker, and session consumers"
            )


def check_signature_requirement_closure(files: dict[Path, str], errors: list[str]) -> None:
    header = files.get(SIGNATURE_FACTS_HEADER, "")
    source = files.get(SIGNATURE_FACTS_SOURCE, "")
    if (
        "struct MarkerShapeModuleInput final {\n"
        "  const ownership::OwnershipAdmittedBoundModule& boundModule;"
        not in header
    ):
        errors.append(
            f"{SIGNATURE_FACTS_HEADER}: marker-shape construction must require ownership admission"
        )
    if "const ownership::OwnershipAdmittedBoundModule& boundModule;" not in header:
        errors.append(
            f"{SIGNATURE_FACTS_HEADER}: signature construction must require ownership admission"
        )
    if header.count("zc::Array<uint8_t> canonicalRecord;") != 3:
        errors.append(
            f"{SIGNATURE_FACTS_HEADER}: signature, impl, and marker requirements must "
            "retain exactly three complete canonical records"
        )
    for marker in (
        "struct SignatureDefinitionCensusEntry final",
        "struct ImplAuthorityCensusEntry final",
        "zc::ArrayPtr<const SignatureDefinitionCensusEntry> sourceSignatureCensus;",
        "zc::ArrayPtr<const ImplAuthorityCensusEntry> sourceImplCensus;",
    ):
        if marker not in header:
            errors.append(
                f"{SIGNATURE_FACTS_HEADER}: missing independent source census marker {marker}"
            )
    required_source = (
        "input.requiredSignatures[index].canonicalRecord.asPtr()",
        "input.requiredImplHeads[index].canonicalRecord.asPtr()",
        "input.requiredMarkerFacts[index].canonicalRecord.asPtr()",
        "value.requirement.canonicalRecord = zc::mv(bytes)",
        "ImplHeadRequirement{value.head.impl, zc::heapArray(value.record.asPtr())}",
        "MarkerFactRequirement{value.fact.key, zc::heapArray(value.record.asPtr())}",
        "input.boundModule.definitions().implAuthorities()",
        "sourceSignatureCensus.asPtr(), sourceImplCensus.asPtr()",
        "candidate.signatures.size() < input.sourceSignatureCensus.size()",
        "candidate.implHeads.size() + candidate.markerFacts.size() <",
    )
    for marker in required_source:
        if marker not in source:
            errors.append(
                f"{SIGNATURE_FACTS_SOURCE}: missing complete requirement marker {marker}"
            )


def check_marker_proof_authority(files: dict[Path, str], errors: list[str]) -> None:
    header = files.get(MARKER_PROOF_HEADER, "")
    source = files.get(MARKER_PROOF_SOURCE, "")
    session = files.get(SESSION_SOURCE, "")
    for marker in (
        "class SemanticTypeInterningCapability final",
        "class MarkerProofInput final",
        "class MarkerProofEngine final",
        "using MarkerProofResult =",
        "const body::BodyCheckingInput& bodyInput",
        "from(const body::BodyCheckingInput& bodyInput)",
        "ZC_NODISCARD MarkerProofResult prove(",
    ):
        if marker not in header:
            errors.append(f"{MARKER_PROOF_HEADER}: missing marker proof authority {marker}")
    for marker in (
        "MarkerProofInput::from",
        "const auto& policy = bodyInput.markerPolicies",
        "const auto& standardMarkers = bodyInput.standardMarkers",
        "bodyInput.boundModule.retain()",
        "driver::module_graph_query::CheckerBoundModuleView boundModule;",
        "boundModule(zc::mv(input.impl->boundModule))",
        "SemanticTypeInterningCapability::intern",
        "componentInterner.intern(",
        "coherence.marker(key)",
        "policy.policy(marker)",
        "signature::BuiltinMarkerEvidence",
        "signature::StructuralMarkerEvidence",
        "active.add(key)",
        "auto produced = impl->resolve(marker, subject)",
        "auto verified = impl->resolve(marker, subject)",
    ):
        if marker not in source:
            errors.append(f"{MARKER_PROOF_SOURCE}: missing marker proof closure {marker}")
    explicit = source.find("coherence.marker(key)")
    policy = source.find("policy.policy(marker)")
    builtin = source.find("signature::BuiltinMarkerEvidence")
    structural = source.find("auto result = structural(marker, subject", builtin + 1)
    if min(explicit, policy, builtin, structural) < 0 or not (
        explicit < policy < builtin < structural
    ):
        errors.append(
            f"{MARKER_PROOF_SOURCE}: marker resolution must remain explicit, builtin, then structural"
        )
    if "static zc::Maybe<MarkerProofInput> from(\n      const signature::" in header:
        errors.append(
            f"{MARKER_PROOF_HEADER}: marker proof input bypasses BodyCheckingInput authority"
        )
    for marker in (
        "MarkerProofInput::from(bodyInput)",
        "engine.prove(marker, subject)",
    ):
        if marker not in session:
            errors.append(f"{SESSION_SOURCE}: missing marker proof session wiring {marker}")
    body_input = files.get(BODY_CHECKER_HEADER, "")
    if "driver::module_graph_query::CheckerBoundModuleView boundModule;" not in body_input:
        errors.append(
            f"{BODY_CHECKER_HEADER}: body checking must retain its bound-module lease"
        )
    if "const signature::VerifiedMarkerPolicyRegistry& markerPolicies;" not in body_input:
        errors.append(
            f"{BODY_CHECKER_HEADER}: marker proof policy must be carried by BodyCheckingInput"
        )
    if (
        "const driver::core::VerifiedCoreStandardMarkerAuthority& standardMarkers;"
        not in body_input
    ):
        errors.append(
            f"{BODY_CHECKER_HEADER}: standard marker authority must be carried by BodyCheckingInput"
        )
    for marker in (
        "zc::Maybe<core::VerifiedCoreLibrary> coreLibrary;",
        "impl->coreLibrary = zc::mv(coreLibraries[0]);",
        ".authorityLease().capability().authority()",
    ):
        if marker not in session:
            errors.append(f"{SESSION_SOURCE}: missing retained standard marker authority {marker}")
    for path, text in files.items():
        if path in {MARKER_PROOF_HEADER, MARKER_PROOF_SOURCE} or path.suffix not in {".h", ".cc"}:
            continue
        code = strip_cpp_comments_and_literals(text)
        if re.search(r"\bclass\s+MarkerProofEngine\b", code):
            errors.append(
                f"{path}: duplicate MarkerProofEngine owner is forbidden; {MARKER_PROOF_HEADER} is canonical"
            )
    signature_source = files.get(SIGNATURE_FACTS_SOURCE, "")
    for marker in (
        "buildSourceGenericParameters(input, definition.definition)",
        "if (isNominalDefinition(definitionKind))",
        "definitionKind == identity::DefinitionKind::Field",
        "definitionKind == identity::DefinitionKind::EnumVariant",
    ):
        if marker not in signature_source:
            errors.append(
                f"{SIGNATURE_FACTS_SOURCE}: missing nominal marker-proof producer {marker}"
            )
    marker_test = files.get(MARKER_PROOF_TEST, "")
    if "signature::SignatureFactsBuilder::build(" not in marker_test:
        errors.append(
            f"{MARKER_PROOF_TEST}: marker proof must exercise production signature construction"
        )
    for forbidden in (
        "SignatureFactsVerifier::verify(",
        "SignatureFactsCandidate{",
    ):
        if forbidden in marker_test:
            errors.append(
                f"{MARKER_PROOF_TEST}: manual verified-signature rail is forbidden: {forbidden}"
            )


def check_operator_closure(files: dict[Path, str], errors: list[str]) -> None:
    header = files.get(OPERATOR_KIND_HEADER, "")
    source = files.get(OPERATOR_KIND_SOURCE, "")
    for enum_name, tags in (
        ("PrimitiveOperation", PRIMITIVE_OPERATOR_TAGS),
        ("CompoundAssignmentOperation", COMPOUND_OPERATOR_TAGS),
    ):
        for name, tag in tags:
            marker = rf"\b{re.escape(name)}\s*=\s*0x{tag:02x}\b"
            if not re.search(marker, header, flags=re.IGNORECASE):
                errors.append(
                    f"{OPERATOR_KIND_HEADER}: missing canonical {enum_name} tag "
                    f"{name}=0x{tag:02x}"
                )
    for name, tag in (("Propagate", 0x01), ("ForcedUnwrap", 0x02)):
        if not re.search(
            rf"\b{re.escape(name)}\s*=\s*0x{tag:02x}\b", header,
            flags=re.IGNORECASE,
        ):
            errors.append(
                f"{OPERATOR_KIND_HEADER}: missing canonical ErrorOperatorKind tag "
                f"{name}=0x{tag:02x}"
            )

    for syntax_enum, syntax_name, semantic_enum, semantic_name in AST_OPERATOR_MAPPINGS:
        mapping = re.compile(
            rf"case\s+ast::{re.escape(syntax_enum)}::{re.escape(syntax_name)}\s*:"
            rf"\s*return\s+OperatorKind\s*\(\s*{re.escape(semantic_enum)}::"
            rf"{re.escape(semantic_name)}\s*\)\s*;"
        )
        if not mapping.search(source):
            errors.append(
                f"{OPERATOR_KIND_SOURCE}: missing symbolic mapping "
                f"{syntax_enum}::{syntax_name} -> {semantic_enum}::{semantic_name}"
            )

    required = (
        "zom.checker-operator-kind",
        "result.add(0x01)",
        "result.add(0x02)",
        "result.add(0x03)",
        "result.add(0x04)",
        "OperatorKind(AssignmentOperator{})",
        "OperatorDiagnostic::UnsupportedInterfaceOperation",
        "OperatorDiagnostic::InvalidBinaryOperands",
        "OperatorDiagnostic::InvalidComparisonOperands",
        "OperatorDiagnostic::ConstantArithmeticFailure",
        "sameOperatorKind(value, reconstructed)",
    )
    for marker in required:
        if marker not in source:
            errors.append(f"{OPERATOR_KIND_SOURCE}: missing operator closure marker {marker}")

    numeric_bridge = re.compile(
        r"static_cast\s*<\s*(?:PrimitiveOperation|CompoundAssignmentOperation|"
        r"ErrorOperatorKind)\s*>"
    )
    if numeric_bridge.search(strip_cpp_comments_and_literals(source)):
        errors.append(
            f"{OPERATOR_KIND_SOURCE}: numeric AST-to-semantic operator cast is forbidden"
        )
    duplicate_owner = re.compile(
        r"\benum\s+class\s+(PrimitiveOperation|CompoundAssignmentOperation)\b"
    )
    for path, text in files.items():
        if not is_checker_source(path) or path == OPERATOR_KIND_HEADER:
            continue
        match = duplicate_owner.search(strip_cpp_comments_and_literals(text))
        if match:
            errors.append(
                f"{path}: duplicate {match.group(1)} owner is forbidden; "
                f"{OPERATOR_KIND_HEADER} is canonical"
            )


def check_type_key_pattern_closure(files: dict[Path, str], errors: list[str]) -> None:
    header = files.get(SIGNATURE_FACTS_HEADER, "")
    source = files.get(SIGNATURE_FACTS_SOURCE, "")
    enum_match = re.search(
        r"enum\s+class\s+TypeKeyPatternTag\s*:\s*uint8_t\s*\{(?P<body>.*?)\};",
        header,
        flags=re.DOTALL,
    )
    expected_tags = [(name, tag) for name, _, tag, _ in TYPE_KEY_PATTERN_TAGS]
    if enum_match is None:
        errors.append(f"{SIGNATURE_FACTS_HEADER}: missing TypeKeyPatternTag enum")
    else:
        actual_tags = [
            (name, int(value, 16))
            for name, value in re.findall(
                r"\b([A-Za-z0-9_]+)\s*=\s*0x([0-9A-Fa-f]+)\b",
                enum_match.group("body"),
            )
        ]
        if actual_tags != expected_tags:
            errors.append(
                f"{SIGNATURE_FACTS_HEADER}: TypeKeyPatternTag sequence differs from "
                "the canonical RFC 0015 inventory"
            )

    impl_match = re.search(
        r"struct\s+TypeKeyPattern::Impl\s+final\s*\{.*?"
        r"zc::OneOf<(?P<types>.*?)>\s+value\s*;",
        source,
        flags=re.DOTALL,
    )
    expected_payloads = [payload for _, payload, _, _ in TYPE_KEY_PATTERN_TAGS]
    if impl_match is None:
        errors.append(f"{SIGNATURE_FACTS_SOURCE}: missing TypeKeyPattern payload owner")
    else:
        actual_payloads = [
            item.strip() for item in impl_match.group("types").split(",")
        ]
        if actual_payloads != expected_payloads:
            errors.append(
                f"{SIGNATURE_FACTS_SOURCE}: TypeKeyPattern payload sequence differs from "
                "the canonical RFC 0015 inventory"
            )

    for name, payload, tag, factory in TYPE_KEY_PATTERN_TAGS:
        factory_marker = f"TypeKeyPattern TypeKeyPattern::{factory}"
        if factory_marker not in source:
            errors.append(
                f"{SIGNATURE_FACTS_SOURCE}: missing TypeKeyPattern factory {factory}"
            )
        if name == "Parameter":
            projection = "return TypeKeyPatternTag::Parameter;"
        else:
            projection = (
                f"value.is<{payload}>()) return TypeKeyPatternTag::{name};"
            )
        if projection not in source:
            errors.append(
                f"{SIGNATURE_FACTS_SOURCE}: missing TypeKeyPattern tag projection {name}"
            )

    required_header = (
        "class TypeKeyPatternKey final",
        "zc::Maybe<TypeKeyPatternKey> makeTypeKeyPatternKey(",
        "zc::Maybe<TypeKeyPatternKey> decodeTypeKeyPatternKey(",
        "bool typeKeyPatternKeyIsCanonical(",
        "class ImplPatternKey final",
        "zc::Maybe<ImplPatternKey> makeImplPatternKey(",
        "zc::Maybe<ImplPatternKey> decodeImplPatternKey(",
        "bool implPatternIsPublishable(",
        "zc::Maybe<CanonicalTypeHead> implPatternHead(",
    )
    for marker in required_header:
        if marker not in header:
            errors.append(
                f"{SIGNATURE_FACTS_HEADER}: missing pattern closure marker {marker}"
            )

    required_source = (
        "zom.type-key-pattern",
        "zom.impl-pattern",
        "TypeKeyPatternKey::TypeKeyPatternKey",
        "SignatureFactsCanonicalCodec::makeTypeKeyPatternKey",
        "SignatureFactsCanonicalCodec::decodeTypeKeyPatternKey",
        "SignatureFactsCanonicalCodec::typeKeyPatternKeyIsCanonical",
        "SignatureFactsCanonicalCodec::makeImplPatternKey",
        "SignatureFactsCanonicalCodec::decodeImplPatternKey",
        "SignatureFactsCanonicalCodec::implPatternKeyIsCanonical",
        "SignatureFactsCanonicalCodec::implPatternIsPublishable",
        "SignatureFactsCanonicalCodec::implPatternHead",
        "SignatureFactsCanonicalCodec::implPatternIsPublishable(head.pattern,",
        "SignatureFactsCanonicalCodec::implPatternHead(head.pattern)",
        "PatternByteDecoder patternDecoder(decoder, identities)",
        "if (pattern == zc::none || !decoder.finished()) return zc::none;",
        "if (interface == zc::none || self == zc::none || !decoder.finished()) return zc::none;",
        "SignatureFactsCanonicalCodec::makeImplPatternKey(completePattern, input.identities)",
        "SignatureFactsCanonicalCodec::implPatternKeyIsCanonical(head.pattern, identities)",
        "encoder.encodeByteString(head.pattern.bytes())",
        "field.name.encode(key)",
        "encodeDefinition(key, binding.associated)",
    )
    for marker in required_source:
        if marker not in source:
            errors.append(
                f"{SIGNATURE_FACTS_SOURCE}: missing pattern codec marker {marker}"
            )
    if "implPatternParametersAreBound" in header or "implPatternParametersAreBound" in source:
        errors.append("removed implPatternParametersAreBound compatibility API remains")
    domain_counts = {
        "zom.type-key-pattern": 2,
        "zom.impl-pattern": 3,
    }
    for domain, expected_count in domain_counts.items():
        if source.count(domain) != expected_count:
            errors.append(
                f"{SIGNATURE_FACTS_SOURCE}: {domain} must have exactly {expected_count} "
                "canonical owner references"
            )

    encode_start = source.find("bool SignatureFactsCanonicalCodec::encodePattern(")
    encode_end = source.find(
        "zc::Maybe<TypeKeyPatternKey> SignatureFactsCanonicalCodec::makeTypeKeyPatternKey(",
        encode_start,
    )
    if encode_start < 0 or encode_end < 0:
        errors.append(f"{SIGNATURE_FACTS_SOURCE}: missing closed pattern encoder boundary")
    else:
        encoder = source[encode_start:encode_end]
        if re.search(r"encodeUint8\s*\(\s*0x[0-9A-Fa-f]+\s*\)", encoder):
            errors.append(
                f"{SIGNATURE_FACTS_SOURCE}: numeric TypeKeyPattern encoding tag is forbidden"
            )
        for name, _, tag, _ in TYPE_KEY_PATTERN_TAGS:
            marker = (
                "encoder.encodeUint8(static_cast<uint8_t>("
                f"TypeKeyPatternTag::{name}));"
            )
            if marker not in encoder:
                errors.append(
                    f"{SIGNATURE_FACTS_SOURCE}: missing canonical pattern encoding tag "
                    f"{name}=0x{tag:02x}"
                )

    duplicate_owner = re.compile(
        r"\b(?:enum\s+class\s+TypeKeyPatternTag|class\s+TypeKeyPatternKey|"
        r"class\s+ImplPatternKey)\b"
    )
    for path, text in files.items():
        if not is_checker_source(path) or path == SIGNATURE_FACTS_HEADER:
            continue
        if duplicate_owner.search(strip_cpp_comments_and_literals(text)):
            errors.append(
                f"{path}: duplicate TypeKeyPatternTag owner is forbidden; "
                f"{SIGNATURE_FACTS_HEADER} is canonical"
            )

    duplicate_codec = re.compile(
        r"\bSignatureFactsCanonicalCodec::(?:makeTypeKeyPatternKey|decodeTypeKeyPatternKey|"
        r"decodeImplPatternKey|encodePattern)\s*\("
    )
    for path, text in files.items():
        if not is_checker_source(path) or path == SIGNATURE_FACTS_SOURCE:
            continue
        if duplicate_codec.search(strip_cpp_comments_and_literals(text)):
            errors.append(
                f"{path}: duplicate TypeKeyPattern codec owner is forbidden; "
                f"{SIGNATURE_FACTS_SOURCE} is canonical"
            )

    domain_owners = [
        path
        for path, text in files.items()
        if path.suffix == ".cc" and "zom.type-key-pattern" in text
    ]
    if domain_owners != [SIGNATURE_FACTS_SOURCE]:
        errors.append(
            "type-key pattern domain must have exactly one production codec owner"
        )


def check_diagnostic_registry(files: dict[Path, str], errors: list[str]) -> None:
    source = files.get(CHECKER_DIAGNOSTICS, "")
    row_pattern = re.compile(
        r"DIAG\(\s*(\d+)\s*,\s*([A-Za-z0-9_]+)\s*,\s*(k[A-Za-z]+)\s*,\s*"
        r"((?:\"(?:\\.|[^\"\\])*\"\s*)+)\s*,\s*(\d+)\s*\)",
        flags=re.DOTALL,
    )
    rows: dict[int, tuple[str, str, str, int]] = {}
    for match in row_pattern.finditer(source):
        code = int(match.group(1))
        try:
            message = ast.literal_eval(match.group(4))
        except (SyntaxError, ValueError):
            errors.append(f"{CHECKER_DIAGNOSTICS}: diagnostic ZOM{code} has invalid text")
            continue
        row = (match.group(2), match.group(3), message, int(match.group(5)))
        if code in rows:
            errors.append(f"{CHECKER_DIAGNOSTICS}: duplicate diagnostic ZOM{code}")
        rows[code] = row

    for code, name, severity, message, arity in ACCEPTED_CHECKER_DIAGNOSTICS:
        expected = (name, severity, message, arity)
        actual = rows.get(code)
        if actual != expected:
            errors.append(
                f"{CHECKER_DIAGNOSTICS}: diagnostic ZOM{code} differs from its "
                "accepted name, severity, message, or arity"
            )
    for code in REMOVED_CHECKER_DIAGNOSTIC_CODES:
        if code in rows:
            errors.append(
                f"{CHECKER_DIAGNOSTICS}: removed checker diagnostic ZOM{code} remains"
            )

    ownership_source = files.get(CHECKER_SOURCE_DIAGNOSTICS, "")
    ownership_pattern = re.compile(
        r"^CHECKER_(ERROR|WARNING|NOTE)\(([A-Za-z0-9_]+)\)$", re.MULTILINE
    )
    actual_ownership = [match.groups() for match in ownership_pattern.finditer(ownership_source)]
    expected_errors: list[tuple[str, str]] = []
    expected_warnings: list[tuple[str, str]] = []
    expected_notes: list[tuple[str, str]] = []
    for code, name, severity, _message, _arity in ACCEPTED_CHECKER_DIAGNOSTICS:
        if severity == "kError" and (4001 <= code <= 4055 or 4077 <= code <= 4081):
            expected_errors.append(("ERROR", name))
        elif code == 4023:
            expected_warnings.append(("WARNING", name))
        elif 4071 <= code <= 4076:
            expected_notes.append(("NOTE", name))
    expected_ownership = expected_errors + expected_warnings + expected_notes
    if actual_ownership != expected_ownership:
        errors.append(
            f"{CHECKER_SOURCE_DIAGNOSTICS}: severity-partitioned RFC 0005 ownership registry differs from the accepted source diagnostic set"
        )

    checked_facts = files.get(CHECKED_FACTS_HEADER, "")
    for forbidden in (
        "diagnosticArguments",
        "zc::Vector<zc::String> diagnostic",
        "diagnostics::DiagID diagnostic;",
    ):
        if forbidden in checked_facts:
            errors.append(
                f"{CHECKED_FACTS_HEADER}: retained Checker diagnostics must not contain {forbidden}"
            )
    for required in (
        "CheckerErrorId diagnostic;",
        "zc::Vector<CheckerDisplayArgument> arguments;",
        "zc::Vector<CheckerNoteRef> notes;",
        "CheckerRecoveryPolicy recoveryPolicy;",
    ):
        if required not in checked_facts:
            errors.append(
                f"{CHECKED_FACTS_HEADER}: missing structured Checker diagnostic field {required}"
            )


def check_diagnostic_rendering(files: dict[Path, str], errors: list[str]) -> None:
    source = files.get(CHECKER_DIAGNOSTIC_ADAPTER, "")
    for placeholder in PLACEHOLDER_DIAGNOSTIC_RENDERINGS:
        if f'"{placeholder}"' in source:
            errors.append(
                f"{CHECKER_DIAGNOSTIC_ADAPTER}: placeholder Checker diagnostic "
                f"rendering {placeholder!r} is forbidden"
            )
    for placeholder in TYPE_CATEGORY_PLACEHOLDER_RENDERINGS:
        if f'"{placeholder}"' in source:
            errors.append(
                f"{CHECKER_DIAGNOSTIC_ADAPTER}: type-category placeholder Checker "
                f"diagnostic rendering {placeholder!r} is forbidden"
            )


def check_production_session(files: dict[Path, str], errors: list[str]) -> None:
    session = files.get(SESSION_SOURCE, "")
    required = (
        "bool CompilerSession::checkSources()",
        "SignatureFactsBuilder::build",
        "ImportedSignatureViewProjector::build",
        "ModuleInterfaceVerifier::build",
        "CoherenceBuilder::build",
        "BodyFactRequirementInventoryBuilder::build",
        "checker::body::BodyChecker bodyChecker",
        "CheckedFactsSourceRejectionVerifier::verify",
        "CheckedFactsVerifier::verify",
        "stagedCheckedFactsRepository->adopt",
        "DispatchSiteInventoryBuilder::build",
        "DispatchFactsBuilder::build",
        "DispatchFactsVerifier::verify",
        "impl->dispatchFacts = zc::mv(ordinaryDispatchFacts);",
        "impl->checkedFactsRepository = zc::mv(stagedCheckedFactsRepository);",
        "impl->verifiedCheckedSources = true;",
    )
    for marker in required:
        if marker not in session:
            errors.append(f"{SESSION_SOURCE}: missing production Checker marker {marker}")


def check_checked_facts_codec(files: dict[Path, str], errors: list[str]) -> None:
    header = files.get(CHECKED_FACTS_HEADER, "")
    source = files.get(CHECKED_FACTS_SOURCE, "")
    body = files.get(BODY_CHECKER_SOURCE, "")
    body_code = strip_cpp_comments_and_literals(body)
    scalar = strip_cpp_comments_and_literals(files.get(SCALAR_LITERAL_FACTS_SOURCE, ""))
    for marker in (
        "class CheckedFactsCanonicalCodec final",
        "writeCanonicalRecords(CheckedFactsCandidate& candidate,",
        "computeConstantEvaluationRevision(",
        "recordsMatch(const CheckedFactsCandidate& candidate,",
    ):
        if marker not in header:
            errors.append(f"{CHECKED_FACTS_HEADER}: missing canonical checked-facts codec marker {marker}")
    if "CheckedFactsCanonicalCodec::recordsMatch(candidate, input)" not in source:
        errors.append(f"{CHECKED_FACTS_SOURCE}: verifier does not independently re-encode candidate facts")
    if "CheckedFactsCanonicalCodec::writeCanonicalRecords(candidate, codecInput)" not in body:
        errors.append(f"{BODY_CHECKER_SOURCE}: production facts bypass the canonical codec")
    if "CanonicalEncoder" in scalar:
        errors.append(f"{SCALAR_LITERAL_FACTS_SOURCE}: scalar producer owns a duplicate canonical record encoder")
    if "CanonicalEncoder" in body_code:
        errors.append(f"{BODY_CHECKER_SOURCE}: body producer owns a duplicate canonical record encoder")


def check_inference_core(files: dict[Path, str], errors: list[str]) -> None:
    header = files.get(INFERENCE_CONTEXT_HEADER, "")
    source = files.get(INFERENCE_CONTEXT_SOURCE, "")
    recovery = files.get(INFERENCE_RECOVERY_HEADER, "")
    for marker in (
        "class InferenceContextToken final",
        "struct TypeVarTag final",
        "using TypeVarId = identity::StoreHandle<TypeVarTag>",
        "class InferenceType final",
        "class InferenceContext final",
    ):
        if marker not in header:
            errors.append(f"{INFERENCE_CONTEXT_HEADER}: missing inference core marker {marker}")
    for marker in (
        "InferenceContextToken::issue",
        "InferenceContext::issueVariable",
        "InferenceContext::addEquality",
        "InferenceContext::solve",
        "InferenceContext::materialize",
        "InferenceContext::finish",
        "InferenceContext destroyed before finish()",
    ):
        if marker not in source:
            errors.append(f"{INFERENCE_CONTEXT_SOURCE}: missing inference lifecycle marker {marker}")
    if "friend class InferenceContextToken;" not in recovery:
        errors.append(
            f"{INFERENCE_RECOVERY_HEADER}: recovery issuer is not exclusively shared with the inference token"
        )
    for path, text in files.items():
        if path.suffix not in {".h", ".cc"} or path == INFERENCE_CONTEXT_HEADER:
            continue
        if re.search(r"\b(?:struct|class)\s+TypeVarTag\b", strip_cpp_comments_and_literals(text)):
            errors.append(f"{path}: duplicate TypeVarTag owner is forbidden")


def check_wiring(files: dict[Path, str], errors: list[str]) -> None:
    required = (
        (CHECKER_CMAKE, "${CMAKE_CURRENT_SOURCE_DIR}/facts/signature-facts.cc"),
        (CHECKER_CMAKE, "${CMAKE_CURRENT_SOURCE_DIR}/body/body-checker.cc"),
        (CHECKER_CMAKE, "${CMAKE_CURRENT_SOURCE_DIR}/inference/checked-facts.cc"),
        (CHECKER_CMAKE, "${CMAKE_CURRENT_SOURCE_DIR}/facts/checked-facts-repository.cc"),
        (CHECKER_CMAKE, "${CMAKE_CURRENT_SOURCE_DIR}/facts/coherence-facts.cc"),
        (CHECKER_CMAKE, "${CMAKE_CURRENT_SOURCE_DIR}/diagnostics/checker-diagnostic-adapter.cc"),
        (CHECKER_CMAKE, "${CMAKE_CURRENT_SOURCE_DIR}/diagnostics/checker-diagnostic-id.cc"),
        (CHECKER_CMAKE, "${CMAKE_CURRENT_SOURCE_DIR}/operator-kind.cc"),
        (CHECKER_CMAKE, "${CMAKE_CURRENT_SOURCE_DIR}/inference/inference-context.cc"),
        (CHECKER_CMAKE, "${CMAKE_CURRENT_SOURCE_DIR}/body/marker-proof.cc"),
        (DRIVER_CMAKE, "imported-signature-view-projector.cc"),
        (DRIVER_CMAKE, "module-interface.cc"),
        (DRIVER_CMAKE, "coherence-builder.cc"),
        (TEST_CMAKE, "NAME checker-architecture"),
        (TEST_CMAKE, "check-checker-architecture.py --check"),
        (TEST_CMAKE, "NAME checker-architecture-negative"),
        (TEST_CMAKE, "check-checker-architecture.py --self-test"),
    )
    for path, marker in required:
        if marker not in files.get(path, ""):
            errors.append(f"{path}: missing Checker architecture wiring marker: {marker}")


def check_rfc0015_interface_cutover(
    files: dict[Path, str], errors: list[str]
) -> None:
    exact_domains = (
        (SIGNATURE_FACTS_SOURCE, "zom.signature-facts-revision"),
        (CROSS_MODULE_FACTS_SOURCE, "zom.coherence-view"),
        (MODULE_INTERFACE_CONTRACT_SOURCE, "zom.module-interface-revision"),
    )
    for path, domain in exact_domains:
        count = files.get(path, "").count(domain)
        if count != 1:
            errors.append(f"{path}: {domain} must have exactly one production owner, found {count}")

    required = (
        (COHERENCE_FACTS_HEADER, "class CoherenceModuleInput final"),
        (COHERENCE_FACTS_HEADER, "struct CoherenceFailureRef final"),
        (COHERENCE_FACTS_HEADER, "MarkerPolicyRegistryRevision markerPolicyRegistryRevision;"),
        (COHERENCE_FACTS_HEADER, "markerPolicyRegistryRevision()"),
        (COHERENCE_FACTS_SOURCE, "CoherenceModuleInput::publish("),
        (COHERENCE_FACTS_SOURCE, "candidate.markerPolicyRegistryRevision.digest()"),
        (COHERENCE_FACTS_SOURCE, "markerPolicies.revision().digest()"),
        (MODULE_INTERFACE_HEADER, "projectCoherenceInput() const"),
        (MODULE_INTERFACE_HEADER,
         "const ownership::OwnershipAdmittedBoundModule& boundModule;"),
        (MODULE_INTERFACE_SOURCE, "VerifiedModuleInterface::projectCoherenceInput() const"),
        (MODULE_INTERFACE_SOURCE, "Impl(ownership::OwnershipAdmittedBoundModule&& boundModule,"),
        (MODULE_INTERFACE_SOURCE, "input.boundModule.retain(), input.boundModule.semanticContext()"),
        (MODULE_INTERFACE_SOURCE, "evidence.is<checker::signature::ExplicitMarkerEvidence>()"),
        (MODULE_INTERFACE_SOURCE, "zc::Vector<zc::Array<uint8_t>> implHeadRecords;"),
        (MODULE_INTERFACE_SOURCE, "zc::Vector<zc::Array<uint8_t>> markerFactRecords;"),
        (COHERENCE_BUILDER_HEADER,
         "const checker::signature::VerifiedMarkerPolicyRegistry& markerPolicies;"),
        (COHERENCE_BUILDER_SOURCE, "input.markerPolicies.revision()"),
        (COHERENCE_BUILDER_SOURCE, "interface.projectCoherenceInput()"),
    )
    for path, marker in required:
        if marker not in files.get(path, ""):
            errors.append(f"{path}: missing RFC 0015 atomic cutover marker {marker}")

    forbidden = (
        (COHERENCE_FACTS_HEADER, "CoherenceModuleCandidate"),
        (COHERENCE_FACTS_HEADER, "CoherenceSourceFailure"),
        (COHERENCE_FACTS_SOURCE, "markerIsLocal"),
        (COHERENCE_FACTS_SOURCE, "candidate.modules[0].markerPolicyRegistryRevision"),
        (COHERENCE_FACTS_SOURCE, "SignatureFactsCanonicalCodec::encodeImplHead"),
        (COHERENCE_FACTS_SOURCE, "SignatureFactsCanonicalCodec::encodeMarkerFact"),
        (COHERENCE_BUILDER_HEADER, "const type::SemanticTypeStore& semanticTypes;"),
        (COHERENCE_BUILDER_SOURCE, "input.semanticTypes"),
    )
    for path, marker in forbidden:
        if marker in files.get(path, ""):
            errors.append(f"{path}: forbidden RFC 0015 dual-authority marker remains: {marker}")


def check_checked_module_admission(files: dict[Path, str], errors: list[str]) -> None:
    required = (
        (CHECKED_MODULE_HEADER, "const ownership::OwnershipAdmittedBoundModule& boundModule;"),
        (CHECKED_MODULE_SOURCE, "Impl(ownership::OwnershipAdmittedBoundModule&& boundModule,"),
        (CHECKED_MODULE_SOURCE, "input.boundModule.retain(), input.moduleInterface"),
        (
            CHECKED_MODULE_HEADER,
            "ownership::OwnershipAdmittedBoundModule retainAdmittedBoundModule() const;",
        ),
        (
            HIR_MODULE_HEADER,
            "ownership::OwnershipAdmittedBoundModule retainAdmittedBoundModule() const;",
        ),
        (HIR_MODULE_SOURCE, "ownership::OwnershipAdmittedBoundModule boundModule;"),
        (HIR_MODULE_SOURCE, "checkedModule.retainAdmittedBoundModule()"),
        (
            BUILT_MIR_HEADER,
            "ownership::OwnershipAdmittedBoundModule retainAdmittedBoundModule() const;",
        ),
        (BUILT_MIR_SOURCE, "ownership::OwnershipAdmittedBoundModule boundModule;"),
        (BUILT_MIR_SOURCE, "hirModule.retainAdmittedBoundModule()"),
        (OWNERSHIP_OVERLAY_SOURCE, "OwnershipAdmittedBoundModule boundModule;"),
        (OWNERSHIP_OVERLAY_SOURCE, "builtMir.retainAdmittedBoundModule()"),
    )
    for path, marker in required:
        if marker not in files.get(path, ""):
            errors.append(f"{path}: retained ownership admission chain is incomplete")


def check_final_core_interface_boundary(files: dict[Path, str], errors: list[str]) -> None:
    for path in ORDINARY_CORE_INTERFACE_CONSUMERS:
        code = strip_cpp_comments_and_literals(files.get(path, ""))
        for forbidden in BOOTSTRAP_CORE_INTERFACE_TYPES:
            if forbidden in code:
                errors.append(
                    f"{path}: bootstrap-only core interface escapes finalization: {forbidden}"
                )


def analyze(files: dict[Path, str]) -> list[str]:
    errors: list[str] = []
    check_removed_rail(files, errors)
    check_canonical_type_boundary(files, errors)
    check_store_owned_type_admission(files, errors)
    check_signature_prototype_boundary(files, errors)
    check_signature_requirement_closure(files, errors)
    check_marker_proof_authority(files, errors)
    check_operator_closure(files, errors)
    check_type_key_pattern_closure(files, errors)
    check_diagnostic_registry(files, errors)
    check_diagnostic_rendering(files, errors)
    check_inference_core(files, errors)
    check_checked_facts_codec(files, errors)
    check_production_session(files, errors)
    check_rfc0015_interface_cutover(files, errors)
    check_checked_module_admission(files, errors)
    check_final_core_interface_boundary(files, errors)
    check_wiring(files, errors)
    return sorted(set(errors))


def run_check() -> int:
    errors = analyze(production_files())
    if errors:
        print("Checker architecture check failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    print("Checker architecture check passed (single canonical production rail).")
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
    baseline = production_files()
    errors = analyze(baseline)
    if errors:
        print("Checker architecture self-test baseline failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    injected = CHECKER_ROOT / "injected-checker.cc"
    failures: list[str] = []
    failures += expect_rejection(
        baseline,
        "operator tag removed",
        lambda files: remove_once(files, OPERATOR_KIND_HEADER, "StrictNe = 0x1d,"),
        "missing canonical PrimitiveOperation tag StrictNe=0x1d",
    )
    failures += expect_rejection(
        baseline,
        "operator mapping removed",
        lambda files: remove_once(
            files,
            OPERATOR_KIND_SOURCE,
            "case ast::BinaryOperatorKind::StrictNe:\n"
            "      return OperatorKind(PrimitiveOperation::StrictNe);",
        ),
        "missing symbolic mapping BinaryOperatorKind::StrictNe",
    )
    failures += expect_rejection(
        baseline,
        "numeric operator cast",
        lambda files: append_source(
            files,
            OPERATOR_KIND_SOURCE,
            "\nOperatorKind bad(ast::BinaryOperatorKind value) { return OperatorKind("
            "static_cast<PrimitiveOperation>(value)); }\n",
        ),
        "numeric AST-to-semantic operator cast is forbidden",
    )
    failures += expect_rejection(
        baseline,
        "duplicate primitive operation owner",
        lambda files: append_source(
            files,
            CHECKED_FACTS_HEADER,
            "\nenum class PrimitiveOperation : uint8_t { Add = 0x01 };\n",
        ),
        "duplicate PrimitiveOperation owner is forbidden",
    )
    failures += expect_rejection(
        baseline,
        "diagnostic operator subset removed",
        lambda files: remove_once(
            files,
            OPERATOR_KIND_SOURCE,
            "OperatorDiagnostic::InvalidComparisonOperands",
        ),
        "missing operator closure marker OperatorDiagnostic::InvalidComparisonOperands",
    )
    failures += expect_rejection(
        baseline,
        "type-key pattern tag removed",
        lambda files: remove_once(
            files, SIGNATURE_FACTS_HEADER, "Parameter = 0x11"
        ),
        "TypeKeyPatternTag sequence differs",
    )
    failures += expect_rejection(
        baseline,
        "type-key pattern tag added",
        lambda files: files.__setitem__(
            SIGNATURE_FACTS_HEADER,
            files[SIGNATURE_FACTS_HEADER].replace(
                "Parameter = 0x11", "Parameter = 0x11, Future = 0x12", 1
            ),
        ),
        "TypeKeyPatternTag sequence differs",
    )
    failures += expect_rejection(
        baseline,
        "type-key pattern payload added",
        lambda files: files.__setitem__(
            SIGNATURE_FACTS_SOURCE,
            files[SIGNATURE_FACTS_SOURCE].replace(
                "PatternInterfaceBound, PatternInterfaceSelf, PatternParameter>",
                "PatternInterfaceBound, PatternInterfaceSelf, PatternParameter, PatternFuture>",
                1,
            ),
        ),
        "TypeKeyPattern payload sequence differs",
    )
    failures += expect_rejection(
        baseline,
        "type-key pattern factory removed",
        lambda files: remove_once(
            files,
            SIGNATURE_FACTS_SOURCE,
            "TypeKeyPattern TypeKeyPattern::interfaceSelf",
        ),
        "missing TypeKeyPattern factory interfaceSelf",
    )
    failures += expect_rejection(
        baseline,
        "type-key pattern encoding tag removed",
        lambda files: remove_once(
            files,
            SIGNATURE_FACTS_SOURCE,
            "encoder.encodeUint8(static_cast<uint8_t>(TypeKeyPatternTag::InterfaceSelf));",
        ),
        "missing canonical pattern encoding tag InterfaceSelf=0x10",
    )
    failures += expect_rejection(
        baseline,
        "numeric type-key pattern encoding tag",
        lambda files: files.__setitem__(
            SIGNATURE_FACTS_SOURCE,
            files[SIGNATURE_FACTS_SOURCE].replace(
                "encoder.encodeUint8(static_cast<uint8_t>(TypeKeyPatternTag::InterfaceSelf));",
                "encoder.encodeUint8(0x10);",
                1,
            ),
        ),
        "numeric TypeKeyPattern encoding tag is forbidden",
    )
    failures += expect_rejection(
        baseline,
        "type-key pattern domain removed",
        lambda files: remove_once(
            files, SIGNATURE_FACTS_SOURCE, "zom.type-key-pattern"
        ),
        "zom.type-key-pattern must have exactly 2 canonical owner references",
    )
    failures += expect_rejection(
        baseline,
        "type-key pattern key API removed",
        lambda files: remove_once(
            files, SIGNATURE_FACTS_HEADER, "class TypeKeyPatternKey final"
        ),
        "missing pattern closure marker class TypeKeyPatternKey final",
    )
    failures += expect_rejection(
        baseline,
        "type-key pattern decoder API removed",
        lambda files: remove_once(
            files,
            SIGNATURE_FACTS_HEADER,
            "zc::Maybe<TypeKeyPatternKey> decodeTypeKeyPatternKey(",
        ),
        "missing pattern closure marker zc::Maybe<TypeKeyPatternKey> decodeTypeKeyPatternKey(",
    )
    failures += expect_rejection(
        baseline,
        "type-key pattern decoder implementation removed",
        lambda files: remove_once(
            files,
            SIGNATURE_FACTS_SOURCE,
            "SignatureFactsCanonicalCodec::decodeTypeKeyPatternKey",
        ),
        "missing pattern codec marker SignatureFactsCanonicalCodec::decodeTypeKeyPatternKey",
    )
    failures += expect_rejection(
        baseline,
        "impl-pattern decoder API removed",
        lambda files: remove_once(
            files,
            SIGNATURE_FACTS_HEADER,
            "zc::Maybe<ImplPatternKey> decodeImplPatternKey(",
        ),
        "missing pattern closure marker zc::Maybe<ImplPatternKey> decodeImplPatternKey(",
    )
    failures += expect_rejection(
        baseline,
        "impl-pattern decoder implementation removed",
        lambda files: remove_once(
            files,
            SIGNATURE_FACTS_SOURCE,
            "SignatureFactsCanonicalCodec::decodeImplPatternKey",
        ),
        "missing pattern codec marker SignatureFactsCanonicalCodec::decodeImplPatternKey",
    )
    failures += expect_rejection(
        baseline,
        "duplicate type-key pattern tag owner",
        lambda files: append_source(
            files,
            CHECKED_FACTS_HEADER,
            "\nenum class TypeKeyPatternTag : uint8_t { Parameter = 0x11 };\n",
        ),
        "duplicate TypeKeyPatternTag owner is forbidden",
    )
    failures += expect_rejection(
        baseline,
        "duplicate type-key pattern codec owner",
        lambda files: append_source(
            files,
            CHECKED_FACTS_SOURCE,
            "\nvoid SignatureFactsCanonicalCodec::makeTypeKeyPatternKey() {}\n",
        ),
        "duplicate TypeKeyPattern codec owner is forbidden",
    )
    failures += expect_rejection(
        baseline,
        "impl-pattern production construction removed",
        lambda files: remove_once(
            files,
            SIGNATURE_FACTS_SOURCE,
            "SignatureFactsCanonicalCodec::makeImplPatternKey(completePattern, input.identities)",
        ),
        "missing pattern codec marker SignatureFactsCanonicalCodec::makeImplPatternKey",
    )
    failures += expect_rejection(
        baseline,
        "impl-pattern production verification removed",
        lambda files: remove_once(
            files,
            SIGNATURE_FACTS_SOURCE,
            "SignatureFactsCanonicalCodec::implPatternKeyIsCanonical(head.pattern, identities)",
        ),
        "missing pattern codec marker SignatureFactsCanonicalCodec::implPatternKeyIsCanonical",
    )
    failures += expect_rejection(
        baseline,
        "impl-pattern byte embedding removed",
        lambda files: remove_once(
            files, SIGNATURE_FACTS_SOURCE, "encoder.encodeByteString(head.pattern.bytes())"
        ),
        "missing pattern codec marker encoder.encodeByteString(head.pattern.bytes())",
    )
    failures += expect_rejection(
        baseline,
        "object field name key removed",
        lambda files: remove_once(
            files, SIGNATURE_FACTS_SOURCE, "field.name.encode(key)"
        ),
        "missing pattern codec marker field.name.encode(key)",
    )
    failures += expect_rejection(
        baseline,
        "associated binding identity key removed",
        lambda files: remove_once(
            files,
            SIGNATURE_FACTS_SOURCE,
            "encodeDefinition(key, binding.associated)",
        ),
        "missing pattern codec marker encodeDefinition(key, binding.associated)",
    )
    failures += expect_rejection(
        baseline,
        "impl-pattern publication API removed",
        lambda files: remove_once(
            files, SIGNATURE_FACTS_HEADER, "bool implPatternIsPublishable("
        ),
        "missing pattern closure marker bool implPatternIsPublishable(",
    )
    failures += expect_rejection(
        baseline,
        "impl-pattern head projection API removed",
        lambda files: remove_once(
            files, SIGNATURE_FACTS_HEADER, "zc::Maybe<CanonicalTypeHead> implPatternHead("
        ),
        "missing pattern closure marker zc::Maybe<CanonicalTypeHead> implPatternHead(",
    )
    failures += expect_rejection(
        baseline,
        "impl-pattern publication validation disconnected",
        lambda files: remove_once(
            files,
            SIGNATURE_FACTS_SOURCE,
            "SignatureFactsCanonicalCodec::implPatternIsPublishable(head.pattern,",
        ),
        "missing pattern codec marker SignatureFactsCanonicalCodec::implPatternIsPublishable(head.pattern,",
    )
    failures += expect_rejection(
        baseline,
        "impl-pattern head validation disconnected",
        lambda files: remove_once(
            files,
            SIGNATURE_FACTS_SOURCE,
            "SignatureFactsCanonicalCodec::implPatternHead(head.pattern)",
        ),
        "missing pattern codec marker SignatureFactsCanonicalCodec::implPatternHead(head.pattern)",
    )
    failures += expect_rejection(
        baseline,
        "marker-shape ownership admission removed",
        lambda files: remove_once(
            files,
            SIGNATURE_FACTS_HEADER,
            "struct MarkerShapeModuleInput final {\n"
            "  const ownership::OwnershipAdmittedBoundModule& boundModule;",
        ),
        "marker-shape construction must require ownership admission",
    )
    failures += expect_rejection(
        baseline,
        "complete signature requirement record removed",
        lambda files: remove_once(
            files, SIGNATURE_FACTS_HEADER, "zc::Array<uint8_t> canonicalRecord;"
        ),
        "requirements must retain exactly three complete canonical records",
    )
    failures += expect_rejection(
        baseline,
        "complete signature requirement comparison disconnected",
        lambda files: remove_once(
            files,
            SIGNATURE_FACTS_SOURCE,
            "input.requiredSignatures[index].canonicalRecord.asPtr()",
        ),
        "missing complete requirement marker input.requiredSignatures[index].canonicalRecord.asPtr()",
    )
    failures += expect_rejection(
        baseline,
        "marker proof authority removed",
        lambda files: remove_once(files, MARKER_PROOF_HEADER, "class MarkerProofEngine final"),
        "missing marker proof authority class MarkerProofEngine final",
    )
    failures += expect_rejection(
        baseline,
        "explicit marker precedence disconnected",
        lambda files: remove_once(files, MARKER_PROOF_SOURCE, "coherence.marker(key)"),
        "missing marker proof closure coherence.marker(key)",
    )
    failures += expect_rejection(
        baseline,
        "independent marker proof reconstruction disconnected",
        lambda files: remove_once(
            files, MARKER_PROOF_SOURCE, "auto verified = impl->resolve(marker, subject)"
        ),
        "missing marker proof closure auto verified = impl->resolve(marker, subject)",
    )
    failures += expect_rejection(
        baseline,
        "marker component interning capability removed",
        lambda files: remove_once(
            files, MARKER_PROOF_HEADER, "class SemanticTypeInterningCapability final"
        ),
        "missing marker proof authority class SemanticTypeInterningCapability final",
    )
    failures += expect_rejection(
        baseline,
        "body-checking marker proof issuance bypassed",
        lambda files: remove_once(
            files, SESSION_SOURCE, "MarkerProofInput::from(bodyInput)"
        ),
        "missing marker proof session wiring MarkerProofInput::from(bodyInput)",
    )
    failures += expect_rejection(
        baseline,
        "marker proof bound-module lease retention removed",
        lambda files: remove_once(
            files, MARKER_PROOF_SOURCE, "bodyInput.boundModule.retain()"
        ),
        "missing marker proof closure bodyInput.boundModule.retain()",
    )
    failures += expect_rejection(
        baseline,
        "body-owned standard marker authority removed",
        lambda files: remove_once(
            files,
            BODY_CHECKER_HEADER,
            "const driver::core::VerifiedCoreStandardMarkerAuthority& standardMarkers;",
        ),
        "standard marker authority must be carried by BodyCheckingInput",
    )
    failures += expect_rejection(
        baseline,
        "body-checking bound-module lease retention removed",
        lambda files: remove_once(
            files,
            BODY_CHECKER_HEADER,
            "driver::module_graph_query::CheckerBoundModuleView boundModule;",
        ),
        "body checking must retain its bound-module lease",
    )
    failures += expect_rejection(
        baseline,
        "nominal signature producer disconnected",
        lambda files: remove_once(
            files,
            SIGNATURE_FACTS_SOURCE,
            "if (isNominalDefinition(definitionKind))",
        ),
        "missing nominal marker-proof producer",
    )
    failures += expect_rejection(
        baseline,
        "marker proof source integration replaced by manual facts",
        lambda files: remove_once(
            files, MARKER_PROOF_TEST, "signature::SignatureFactsBuilder::build("
        ),
        "marker proof must exercise production signature construction",
    )
    failures += expect_rejection(
        baseline,
        "signature source census removed",
        lambda files: remove_once(
            files,
            SIGNATURE_FACTS_HEADER,
            "zc::ArrayPtr<const SignatureDefinitionCensusEntry> sourceSignatureCensus;",
        ),
        "missing independent source census marker",
    )
    failures += expect_rejection(
        baseline,
        "impl authority census disconnected",
        lambda files: remove_once(
            files, SIGNATURE_FACTS_SOURCE, "input.boundModule.definitions().implAuthorities()"
        ),
        "missing complete requirement marker input.boundModule.definitions().implAuthorities()",
    )
    failures += expect_rejection(
        baseline,
        "accepted diagnostic text changed",
        lambda files: remove_once(
            files,
            CHECKER_DIAGNOSTICS,
            "Operator {0} implementation for {1} has an incompatible signature",
        ),
        "diagnostic ZOM4019 differs from its accepted",
    )
    failures += expect_rejection(
        baseline,
        "removed diagnostic restored",
        lambda files: append_source(
            files,
            CHECKER_DIAGNOSTICS,
            '\nDIAG(4027, UndeclaredValue, kError, "undeclared", 0)\n',
        ),
        "removed checker diagnostic ZOM4027 remains",
    )
    failures += expect_rejection(
        baseline,
        "source diagnostic ownership removed",
        lambda files: remove_once(
            files, CHECKER_SOURCE_DIAGNOSTICS, "CHECKER_ERROR(BodyLiteralOutOfRange)"
        ),
        "severity-partitioned RFC 0005 ownership registry differs",
    )
    failures += expect_rejection(
        baseline,
        "rendered strings retained in checker failure",
        lambda files: append_source(
            files, CHECKED_FACTS_HEADER, "\nzc::Vector<zc::String> diagnosticArguments;\n"
        ),
        "retained Checker diagnostics must not contain diagnosticArguments",
    )
    for placeholder in PLACEHOLDER_DIAGNOSTIC_RENDERINGS:
        failures += expect_rejection(
            baseline,
            f"diagnostic placeholder {placeholder}",
            lambda files, placeholder=placeholder: append_source(
                files,
                CHECKER_DIAGNOSTIC_ADAPTER,
                f'\nzc::String injectedPlaceholder() {{ return zc::str("{placeholder}"); }}\n',
            ),
            f"placeholder Checker diagnostic rendering {placeholder!r} is forbidden",
        )
    for placeholder in TYPE_CATEGORY_PLACEHOLDER_RENDERINGS:
        failures += expect_rejection(
            baseline,
            f"type-category diagnostic placeholder {placeholder}",
            lambda files, placeholder=placeholder: append_source(
                files,
                CHECKER_DIAGNOSTIC_ADAPTER,
                f'\nzc::String injectedTypePlaceholder() {{ return zc::str("{placeholder}"); }}\n',
            ),
            f"type-category placeholder Checker diagnostic rendering "
            f"{placeholder!r} is forbidden",
        )
    failures += expect_rejection(
        baseline,
        "inference token removed",
        lambda files: remove_once(
            files, INFERENCE_CONTEXT_HEADER, "class InferenceContextToken final"
        ),
        "missing inference core marker class InferenceContextToken final",
    )
    failures += expect_rejection(
        baseline,
        "duplicate type variable owner",
        lambda files: files.__setitem__(
            injected, files.get(injected, "") + "\nstruct TypeVarTag final {};\n"
        ),
        "duplicate TypeVarTag owner is forbidden",
    )
    failures += expect_rejection(
        baseline,
        "TypeEnv dependency",
        lambda files: files.__setitem__(
            injected, '#include "zomlang/compiler/type/type-env.h"\nTypeEnv* value;\n'
        ),
        "non-canonical Checker type dependency",
    )
    failures += expect_rejection(
        baseline,
        "public semantic type key resolver",
        lambda files: append_source(
            files,
            SEMANTIC_TYPE_KEY_HEADER,
            "\nclass SemanticTypeKeyResolver { public: virtual void resolve() = 0; };\n",
        ),
        "public SemanticTypeKeyResolver capability is forbidden",
    )
    failures += expect_rejection(
        baseline,
        "raw semantic type canonicalizer",
        lambda files: append_source(
            files,
            SEMANTIC_TYPE_KEY_HEADER,
            "\nclass TypeCanonicalizer { public: static void canonicalizeClosed(); };\n",
        ),
        "raw TypeCanonicalizer canonicalization entry is forbidden",
    )
    failures += expect_rejection(
        baseline,
        "external canonical type data construction",
        lambda files: append_source(
            files,
            SEMANTIC_TYPE_KEY_SOURCE,
            "\nvoid escapeCanonicalTypeData() { semantic::CanonicalTypeData(1); }\n",
        ),
        "CanonicalTypeData construction is forbidden outside SemanticTypeStore",
    )
    failures += expect_rejection(
        baseline,
        "removed Checker source",
        lambda files: files.__setitem__(CHECKER_ROOT / "checker.cc", "void check();\n"),
        "removed Checker rail must not exist",
    )
    failures += expect_rejection(
        baseline,
        "internType entry",
        lambda files: append_source(files, SIGNATURE_FACTS_SOURCE, "\nvoid f(auto& x) { x.internType(1); }\n"),
        "internType compatibility entry is forbidden",
    )
    failures += expect_rejection(
        baseline,
        "prototype verifier escape",
        lambda files: append_source(files, SESSION_SOURCE, "\nSignatureFactsVerifier escaped;\n"),
        "signature prototype capability SignatureFactsVerifier is forbidden",
    )
    failures += expect_rejection(
        baseline,
        "verified facts escape",
        lambda files: append_source(
            files,
            Path("zomlang/compiler/binder/binding-run.cc"),
            "\nVerifiedSignatureFacts escaped;\n",
        ),
        "verified signature capability is forbidden outside",
    )
    failures += expect_rejection(
        baseline,
        "ad hoc production checker",
        lambda files: files.__setitem__(injected, "ProductionChecker checker;\n"),
        "ad hoc Checker capability ProductionChecker is forbidden",
    )
    failures += expect_rejection(
        baseline,
        "checked-facts producer codec removed",
        lambda files: remove_once(
            files,
            BODY_CHECKER_SOURCE,
            "CheckedFactsCanonicalCodec::writeCanonicalRecords(candidate, codecInput)",
        ),
        "production facts bypass the canonical codec",
    )
    failures += expect_rejection(
        baseline,
        "source rejection verification stage removed",
        lambda files: remove_once(
            files, SESSION_SOURCE, "CheckedFactsSourceRejectionVerifier::verify"
        ),
        "missing production Checker marker",
    )
    failures += expect_rejection(
        baseline,
        "production verification stage removed",
        lambda files: remove_once(files, SESSION_SOURCE, "CheckedFactsVerifier::verify"),
        "missing production Checker marker",
    )
    failures += expect_rejection(
        baseline,
        "atomic checked-facts publication removed",
        lambda files: remove_once(
            files,
            SESSION_SOURCE,
            "impl->checkedFactsRepository = zc::mv(stagedCheckedFactsRepository);",
        ),
        "missing production Checker marker",
    )
    failures += expect_rejection(
        baseline,
        "atomic dispatch-facts publication removed",
        lambda files: remove_once(
            files,
            SESSION_SOURCE,
            "impl->dispatchFacts = zc::mv(ordinaryDispatchFacts);",
        ),
        "missing production Checker marker",
    )
    failures += expect_rejection(
        baseline,
        "negative gate disconnected",
        lambda files: remove_once(files, TEST_CMAKE, "check-checker-architecture.py --self-test"),
        "missing Checker architecture wiring marker",
    )
    failures += expect_rejection(
        baseline,
        "coherence root policy lineage removed",
        lambda files: remove_once(
            files,
            COHERENCE_FACTS_HEADER,
            "signature::MarkerPolicyRegistryRevision markerPolicyRegistryRevision;",
        ),
        "missing RFC 0015 atomic cutover marker",
    )
    failures += expect_rejection(
        baseline,
        "forgeable coherence module input restored",
        lambda files: remove_once(
            files, COHERENCE_FACTS_HEADER, "class CoherenceModuleInput final"
        ),
        "missing RFC 0015 atomic cutover marker",
    )
    failures += expect_rejection(
        baseline,
        "coherence semantic store authority restored",
        lambda files: append_source(
            files,
            COHERENCE_BUILDER_HEADER,
            "\nconst type::SemanticTypeStore& semanticTypes;\n",
        ),
        "forbidden RFC 0015 dual-authority marker remains",
    )
    failures += expect_rejection(
        baseline,
        "checked-module ownership admission removed",
        lambda files: remove_once(
            files,
            CHECKED_MODULE_HEADER,
            "const ownership::OwnershipAdmittedBoundModule& boundModule;",
        ),
        "retained ownership admission chain is incomplete",
    )
    failures += expect_rejection(
        baseline,
        "bootstrap core interface escape",
        lambda files: append_source(
            files,
            Path("zomlang/compiler/driver/interface/borrow-evidence.cc"),
            "\nMaterializeCoreBootstrapModuleInterfaceQuery escaped;\n",
        ),
        "bootstrap-only core interface escapes finalization",
    )
    failures += expect_rejection(
        baseline,
        "HIR ownership admission retention removed",
        lambda files: remove_once(
            files,
            HIR_MODULE_HEADER,
            "ownership::OwnershipAdmittedBoundModule retainAdmittedBoundModule() const;",
        ),
        "retained ownership admission chain is incomplete",
    )
    failures += expect_rejection(
        baseline,
        "module-interface ownership admission removed",
        lambda files: remove_once(
            files,
            MODULE_INTERFACE_HEADER,
            "const ownership::OwnershipAdmittedBoundModule& boundModule;",
        ),
        "missing RFC 0015 atomic cutover marker",
    )
    failures += expect_rejection(
        baseline,
        "module explicit marker boundary removed",
        lambda files: remove_once(
            files,
            MODULE_INTERFACE_SOURCE,
            "evidence.is<checker::signature::ExplicitMarkerEvidence>()",
        ),
        "missing RFC 0015 atomic cutover marker",
    )
    failures += expect_rejection(
        baseline,
        "global marker orphan authority restored",
        lambda files: append_source(files, COHERENCE_FACTS_SOURCE, "\nbool markerIsLocal;\n"),
        "forbidden RFC 0015 dual-authority marker remains",
    )
    failures += expect_rejection(
        baseline,
        "coherence policy registry input removed",
        lambda files: remove_once(
            files,
            COHERENCE_BUILDER_HEADER,
            "const checker::signature::VerifiedMarkerPolicyRegistry& markerPolicies;",
        ),
        "missing RFC 0015 atomic cutover marker",
    )
    failures += expect_rejection(
        baseline,
        "verified interface projection bypassed",
        lambda files: remove_once(
            files, COHERENCE_BUILDER_SOURCE, "interface.projectCoherenceInput()"
        ),
        "missing RFC 0015 atomic cutover marker",
    )

    if failures:
        print("Checker architecture self-test failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    fixture_count = 73 + len(PLACEHOLDER_DIAGNOSTIC_RENDERINGS) + len(
        TYPE_CATEGORY_PLACEHOLDER_RENDERINGS
    )
    print(
        f"Checker architecture negative fixtures passed "
        f"({fixture_count}/{fixture_count})."
    )
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
