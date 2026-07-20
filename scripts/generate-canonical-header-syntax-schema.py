#!/usr/bin/env python3

from __future__ import annotations

import argparse
import contextlib
import copy
import hashlib
import io
import json
import re
import sys
import tempfile
from pathlib import Path

import yaml


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SCHEMA = (
    ROOT / "products/zomlang/compiler/identity/canonical-header-syntax-schema.yml"
)
DEFAULT_OUTPUT = (
    ROOT / "products/zomlang/compiler/identity/canonical-header-syntax-schema.def"
)

EXPECTED_ENUMS = [
    ("CallableHeaderKind", [("Function", 0x01), ("Method", 0x02), ("Constructor", 0x03)]),
    ("ReceiverShape", [("Shared", 0x01), ("Mutable", 0x02), ("Move", 0x03)]),
    ("ExternalAbi", [("Cdecl", 0x01), ("Stdcall", 0x02), ("ZomNative", 0x03)]),
    (
        "PredefinedTypeKind",
        [
            ("I8", 0x01),
            ("I16", 0x02),
            ("I32", 0x03),
            ("I64", 0x04),
            ("U8", 0x05),
            ("U16", 0x06),
            ("U32", 0x07),
            ("U64", 0x08),
            ("F32", 0x09),
            ("F64", 0x0A),
            ("Bool", 0x0B),
            ("Str", 0x0C),
            ("Char", 0x0D),
            ("Null", 0x0E),
            ("Unit", 0x0F),
            ("Never", 0x10),
            ("Any", 0x11),
        ],
    ),
    ("ReferenceMutability", [("Shared", 0x01), ("Mutable", 0x02)]),
    ("RawPointerMutability", [("Const", 0x01), ("Mutable", 0x02)]),
]

EXPECTED_SUMS = [
    (
        "CanonicalCallableResult",
        [
            ("Unit", 0x01, []),
            ("ConstructorSelf", 0x02, []),
            ("Type", 0x03, [("type", "CanonicalHeaderTypeSyntax")]),
        ],
    ),
    (
        "CanonicalNameRoot",
        [
            ("Absolute", 0x01, []),
            ("Relative", 0x02, []),
            ("Generic", 0x03, [("binderDepth", "uint32"), ("ordinal", "uint32")]),
        ],
    ),
    (
        "CanonicalHeaderTypeSyntax",
        [
            (
                "Named",
                0x01,
                [
                    ("name", "CanonicalNameReference"),
                    ("arguments", "Sequence<CanonicalHeaderTypeSyntax>"),
                ],
            ),
            ("Predefined", 0x02, [("kind", "PredefinedTypeKind")]),
            (
                "Function",
                0x03,
                [
                    ("parameters", "Sequence<CanonicalHeaderTypeSyntax>"),
                    ("result", "CanonicalHeaderTypeSyntax"),
                    (
                        "raises",
                        "Maybe<SortedUniqueSequence<CanonicalHeaderTypeSyntax>>",
                    ),
                ],
            ),
            (
                "Union",
                0x04,
                [("members", "SortedUniqueNonEmptySequence<CanonicalHeaderTypeSyntax>")],
            ),
            (
                "Intersection",
                0x05,
                [("members", "SortedUniqueNonEmptySequence<CanonicalHeaderTypeSyntax>")],
            ),
            (
                "FixedArray",
                0x06,
                [("element", "CanonicalHeaderTypeSyntax"), ("length", "uint64")],
            ),
            ("DynamicArray", 0x07, [("element", "CanonicalHeaderTypeSyntax")]),
            ("Slice", 0x08, [("element", "CanonicalHeaderTypeSyntax")]),
            (
                "Optional",
                0x09,
                [("element", "CanonicalHeaderTypeSyntax"), ("depth", "uint8")],
            ),
            (
                "Reference",
                0x0A,
                [
                    ("mutability", "ReferenceMutability"),
                    ("element", "CanonicalHeaderTypeSyntax"),
                ],
            ),
            (
                "RawPointer",
                0x0B,
                [
                    ("mutability", "RawPointerMutability"),
                    ("element", "CanonicalHeaderTypeSyntax"),
                ],
            ),
            ("TypeQuery", 0x0C, [("name", "CanonicalNameReference")]),
            (
                "Object",
                0x0D,
                [("members", "SortedUniqueSequence<CanonicalObjectTypeMember>")],
            ),
            ("Tuple", 0x0E, [("elements", "Sequence<CanonicalHeaderTypeSyntax>")]),
            (
                "AssociatedProjection",
                0x0F,
                [
                    ("base", "CanonicalHeaderTypeSyntax"),
                    ("interface", "Maybe<CanonicalHeaderTypeSyntax>"),
                    ("member", "NfcName"),
                ],
            ),
            (
                "Dynamic",
                0x10,
                [
                    ("principal", "CanonicalNamedHeaderType"),
                    ("markers", "SortedUniqueSequence<CanonicalNameReference>"),
                    (
                        "associatedBindings",
                        "SortedUniqueSequence<CanonicalAssociatedBinding>",
                    ),
                ],
            ),
        ],
    ),
]

EXPECTED_RECORDS = [
    (
        "CanonicalOverloadHeader",
        [
            ("callableKind", "CallableHeaderKind"),
            ("name", "NfcDeclaredName"),
            ("receiver", "Maybe<ReceiverShape>"),
            ("genericParameters", "Sequence<CanonicalGenericParameter>"),
            ("obligations", "SortedUniqueSequence<CanonicalBoundObligation>"),
            ("parameters", "Sequence<CanonicalCallableParameter>"),
            ("result", "CanonicalCallableResult"),
            ("raises", "Maybe<SortedUniqueSequence<CanonicalHeaderTypeSyntax>>"),
            ("externalAbi", "Maybe<ExternalAbi>"),
        ],
    ),
    ("CanonicalGenericParameter", [("defaultType", "Maybe<CanonicalHeaderTypeSyntax>")]),
    (
        "CanonicalBoundObligation",
        [("subject", "CanonicalHeaderTypeSyntax"), ("bound", "CanonicalHeaderTypeSyntax")],
    ),
    (
        "CanonicalCallableParameter",
        [
            ("label", "NfcName"),
            ("type", "CanonicalHeaderTypeSyntax"),
            ("hasDefault", "bool"),
        ],
    ),
    (
        "CanonicalNameReference",
        [("root", "CanonicalNameRoot"), ("suffix", "Sequence<NfcName>")],
    ),
    (
        "CanonicalNamedHeaderType",
        [
            ("name", "CanonicalNameReference"),
            ("arguments", "Sequence<CanonicalHeaderTypeSyntax>"),
        ],
    ),
    (
        "CanonicalObjectTypeMember",
        [
            ("name", "NfcName"),
            ("type", "CanonicalHeaderTypeSyntax"),
            ("mutable", "bool"),
            ("optional", "bool"),
        ],
    ),
    (
        "CanonicalAssociatedBinding",
        [("name", "NfcName"), ("type", "CanonicalHeaderTypeSyntax")],
    ),
]

EXPECTED_CONSTRAINTS = [
    ("CanonicalOverloadHeader.raises", "PresentSequenceMustBeNonEmpty"),
    ("CanonicalNameReference.root", "AbsoluteAndRelativeRequireNonEmptySuffix"),
    ("CanonicalNameReference.root", "GenericPermitsEmptySuffix"),
    ("CanonicalHeaderTypeSyntax.Function.raises", "PresentSequenceMustBeNonEmpty"),
    (
        "CanonicalHeaderTypeSyntax.Union.members",
        "FlattenSameVariantSortUniqueCollapseSingleton",
    ),
    (
        "CanonicalHeaderTypeSyntax.Intersection.members",
        "FlattenSameVariantSortUniqueCollapseSingleton",
    ),
    ("CanonicalHeaderTypeSyntax.FixedArray.length", "EvaluatedUnsigned64"),
    ("CanonicalHeaderTypeSyntax.Optional.depth", "Exactly0x01Or0x02"),
    (
        "CanonicalHeaderTypeSyntax.Dynamic.principal",
        "ExactlyOneCanonicalNamedHeaderType",
    ),
    ("CanonicalHeaderTypeSyntax.Dynamic", "NoLifetimeField"),
]

FORBIDDEN_AST_VALUE = re.compile(
    r"(?:^|[^A-Za-z0-9_])(?:Ast|AST|NodeId|SourceRange|SourceSpan|Token|Arena|Parser|Recovery)(?:$|[^A-Za-z0-9_])"
)


class SchemaError(ValueError):
    pass


def expected_schema() -> dict[str, object]:
    return {
        "version": 1,
        "domain": "zom.canonical-header-syntax.v0",
        "enums": [
            {
                "name": name,
                "wire_type": "uint8",
                "values": [{"name": value, "tag": tag} for value, tag in values],
            }
            for name, values in EXPECTED_ENUMS
        ],
        "sums": [
            {
                "name": name,
                "wire_type": "uint8",
                "variants": [
                    {
                        "name": variant,
                        "tag": tag,
                        "fields": [
                            {"name": field_name, "type": field_type}
                            for field_name, field_type in fields
                        ],
                    }
                    for variant, tag, fields in variants
                ],
            }
            for name, variants in EXPECTED_SUMS
        ],
        "records": [
            {
                "name": name,
                "fields": [
                    {"name": field_name, "type": field_type}
                    for field_name, field_type in fields
                ],
            }
            for name, fields in EXPECTED_RECORDS
        ],
        "constraints": [
            {"target": target, "rule": rule} for target, rule in EXPECTED_CONSTRAINTS
        ],
    }


def first_difference(expected: object, actual: object, path: str = "schema") -> str | None:
    if type(expected) is not type(actual):
        return f"{path}: expected {type(expected).__name__}, found {type(actual).__name__}"
    if isinstance(expected, dict):
        expected_keys = list(expected)
        actual_keys = list(actual)  # type: ignore[arg-type]
        if expected_keys != actual_keys:
            return f"{path}: expected keys {expected_keys}, found {actual_keys}"
        for key in expected_keys:
            difference = first_difference(expected[key], actual[key], f"{path}.{key}")  # type: ignore[index]
            if difference is not None:
                return difference
        return None
    if isinstance(expected, list):
        if len(expected) != len(actual):  # type: ignore[arg-type]
            return f"{path}: expected {len(expected)} entries, found {len(actual)}"  # type: ignore[arg-type]
        for index, (expected_item, actual_item) in enumerate(zip(expected, actual)):  # type: ignore[arg-type]
            difference = first_difference(expected_item, actual_item, f"{path}[{index}]")
            if difference is not None:
                return difference
        return None
    if expected != actual:
        return f"{path}: expected {expected!r}, found {actual!r}"
    return None


def scalar_values(value: object) -> list[str]:
    if isinstance(value, dict):
        result: list[str] = []
        for key, child in value.items():
            result.extend((str(key), *scalar_values(child)))
        return result
    if isinstance(value, list):
        result = []
        for child in value:
            result.extend(scalar_values(child))
        return result
    return [str(value)]


def load_schema(path: Path) -> dict[str, object]:
    try:
        value = yaml.safe_load(path.read_text(encoding="utf-8"))
    except (OSError, yaml.YAMLError) as error:
        raise SchemaError(f"cannot read schema: {error}") from error
    if not isinstance(value, dict):
        raise SchemaError("schema root must be a mapping")
    return value


def validate_schema(schema: dict[str, object]) -> None:
    for value in scalar_values(schema):
        if FORBIDDEN_AST_VALUE.search(value):
            raise SchemaError(f"AST or provenance value is forbidden: {value}")

    difference = first_difference(expected_schema(), schema)
    if difference is not None:
        raise SchemaError(difference)

    for enum in schema["enums"]:  # type: ignore[index]
        tags = [value["tag"] for value in enum["values"]]  # type: ignore[index]
        if len(tags) != len(set(tags)):
            raise SchemaError(f"enum {enum['name']} contains duplicate tags")  # type: ignore[index]
    for sum_type in schema["sums"]:  # type: ignore[index]
        tags = [variant["tag"] for variant in sum_type["variants"]]  # type: ignore[index]
        if len(tags) != len(set(tags)):
            raise SchemaError(f"sum {sum_type['name']} contains duplicate tags")  # type: ignore[index]


def canonical_schema_digest(schema: dict[str, object]) -> str:
    encoded = json.dumps(schema, ensure_ascii=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def render_definition(schema: dict[str, object]) -> str:
    lines = [
        "// Copyright (c) 2026 Zode.Z. All rights reserved",
        "// Generated by scripts/generate-canonical-header-syntax-schema.py.",
        "// Do not edit by hand.",
        f"// Schema domain: {schema['domain']}",
        f"// Canonical schema SHA-256: {canonical_schema_digest(schema)}",
        "",
        "#ifndef ZOM_CANONICAL_HEADER_ENUM_VALUE",
        "#define ZOM_CANONICAL_HEADER_ENUM_VALUE(enumName, valueName, tag)",
        "#define ZOM_CANONICAL_HEADER_ENUM_VALUE_OWNED",
        "#endif",
        "",
    ]
    for enum in schema["enums"]:  # type: ignore[index]
        for value in enum["values"]:  # type: ignore[index]
            lines.append(
                f"ZOM_CANONICAL_HEADER_ENUM_VALUE({enum['name']}, {value['name']}, 0x{value['tag']:02x})"
            )

    lines.extend(
        [
            "",
            "#ifndef ZOM_CANONICAL_HEADER_SUM_VARIANT",
            "#define ZOM_CANONICAL_HEADER_SUM_VARIANT(sumName, variantName, tag)",
            "#define ZOM_CANONICAL_HEADER_SUM_VARIANT_OWNED",
            "#endif",
            "",
        ]
    )
    for sum_type in schema["sums"]:  # type: ignore[index]
        for variant in sum_type["variants"]:  # type: ignore[index]
            lines.append(
                f"ZOM_CANONICAL_HEADER_SUM_VARIANT({sum_type['name']}, {variant['name']}, 0x{variant['tag']:02x})"
            )

    lines.extend(
        [
            "",
            "#ifndef ZOM_CANONICAL_HEADER_VARIANT_FIELD",
            "#define ZOM_CANONICAL_HEADER_VARIANT_FIELD(sumName, variantName, ordinal, fieldName, fieldType)",
            "#define ZOM_CANONICAL_HEADER_VARIANT_FIELD_OWNED",
            "#endif",
            "",
        ]
    )
    for sum_type in schema["sums"]:  # type: ignore[index]
        for variant in sum_type["variants"]:  # type: ignore[index]
            for ordinal, field in enumerate(variant["fields"]):  # type: ignore[index]
                lines.append(
                    "ZOM_CANONICAL_HEADER_VARIANT_FIELD("
                    f"{sum_type['name']}, {variant['name']}, {ordinal}, "
                    f"{field['name']}, {field['type']})"
                )

    lines.extend(
        [
            "",
            "#ifndef ZOM_CANONICAL_HEADER_RECORD_FIELD",
            "#define ZOM_CANONICAL_HEADER_RECORD_FIELD(recordName, ordinal, fieldName, fieldType)",
            "#define ZOM_CANONICAL_HEADER_RECORD_FIELD_OWNED",
            "#endif",
            "",
        ]
    )
    for record in schema["records"]:  # type: ignore[index]
        for ordinal, field in enumerate(record["fields"]):  # type: ignore[index]
            lines.append(
                "ZOM_CANONICAL_HEADER_RECORD_FIELD("
                f"{record['name']}, {ordinal}, {field['name']}, {field['type']})"
            )

    lines.extend(
        [
            "",
            "#ifndef ZOM_CANONICAL_HEADER_CONSTRAINT",
            "#define ZOM_CANONICAL_HEADER_CONSTRAINT(target, rule)",
            "#define ZOM_CANONICAL_HEADER_CONSTRAINT_OWNED",
            "#endif",
            "",
        ]
    )
    for constraint in schema["constraints"]:  # type: ignore[index]
        lines.append(
            f"ZOM_CANONICAL_HEADER_CONSTRAINT(\"{constraint['target']}\", \"{constraint['rule']}\")"
        )

    lines.extend(
        [
            "",
            "#ifdef ZOM_CANONICAL_HEADER_ENUM_VALUE_OWNED",
            "#undef ZOM_CANONICAL_HEADER_ENUM_VALUE",
            "#undef ZOM_CANONICAL_HEADER_ENUM_VALUE_OWNED",
            "#endif",
            "#ifdef ZOM_CANONICAL_HEADER_SUM_VARIANT_OWNED",
            "#undef ZOM_CANONICAL_HEADER_SUM_VARIANT",
            "#undef ZOM_CANONICAL_HEADER_SUM_VARIANT_OWNED",
            "#endif",
            "#ifdef ZOM_CANONICAL_HEADER_VARIANT_FIELD_OWNED",
            "#undef ZOM_CANONICAL_HEADER_VARIANT_FIELD",
            "#undef ZOM_CANONICAL_HEADER_VARIANT_FIELD_OWNED",
            "#endif",
            "#ifdef ZOM_CANONICAL_HEADER_RECORD_FIELD_OWNED",
            "#undef ZOM_CANONICAL_HEADER_RECORD_FIELD",
            "#undef ZOM_CANONICAL_HEADER_RECORD_FIELD_OWNED",
            "#endif",
            "#ifdef ZOM_CANONICAL_HEADER_CONSTRAINT_OWNED",
            "#undef ZOM_CANONICAL_HEADER_CONSTRAINT",
            "#undef ZOM_CANONICAL_HEADER_CONSTRAINT_OWNED",
            "#endif",
            "",
        ]
    )
    return "\n".join(lines)


def generated_text(schema_path: Path) -> str:
    schema = load_schema(schema_path)
    validate_schema(schema)
    return render_definition(schema)


def run_check(schema_path: Path, output_path: Path) -> int:
    expected = generated_text(schema_path)
    try:
        actual = output_path.read_text(encoding="utf-8")
    except OSError as error:
        print(f"error: cannot read generated definition: {error}", file=sys.stderr)
        return 1
    if actual != expected:
        print(
            "error: generated canonical header syntax definition is stale; "
            "run scripts/generate-canonical-header-syntax-schema.py",
            file=sys.stderr,
        )
        return 1
    print("canonical header syntax schema check passed")
    return 0


def expect_schema_failure(name: str, schema: dict[str, object]) -> bool:
    try:
        validate_schema(schema)
    except SchemaError:
        print(f"negative fixture passed: {name}")
        return True
    print(f"error: negative fixture did not fail: {name}", file=sys.stderr)
    return False


def run_self_test(schema_path: Path) -> int:
    baseline = load_schema(schema_path)
    validate_schema(baseline)

    cases: list[tuple[str, dict[str, object]]] = []
    tag_drift = copy.deepcopy(baseline)
    tag_drift["sums"][2]["variants"][0]["tag"] = 0x02  # type: ignore[index]
    cases.append(("header type tag drift", tag_drift))

    field_drift = copy.deepcopy(baseline)
    field_drift["records"][0]["fields"][1]["name"] = "sourceName"  # type: ignore[index]
    cases.append(("overload header field drift", field_drift))

    ast_id = copy.deepcopy(baseline)
    ast_id["sums"][2]["variants"][0]["fields"].append(  # type: ignore[index]
        {"name": "syntax", "type": "NodeId"}
    )
    cases.append(("AST id field", ast_id))

    unknown_variant = copy.deepcopy(baseline)
    unknown_variant["sums"][2]["variants"].append(  # type: ignore[index]
        {"name": "Unknown", "tag": 0x11, "fields": []}
    )
    cases.append(("unknown header type variant", unknown_variant))

    normalization_drift = copy.deepcopy(baseline)
    normalization_drift["constraints"][4]["rule"] = "PreserveSourceOrder"  # type: ignore[index]
    cases.append(("normalization drift", normalization_drift))

    if not all(expect_schema_failure(name, schema) for name, schema in cases):
        return 1

    with tempfile.TemporaryDirectory(prefix="zom-header-schema-") as directory:
        output = Path(directory) / "schema.def"
        output.write_text(render_definition(baseline) + "// drift\n", encoding="utf-8")
        with contextlib.redirect_stderr(io.StringIO()):
            drift_result = run_check(schema_path, output)
        if drift_result == 0:
            print("error: negative fixture did not fail: generated definition drift", file=sys.stderr)
            return 1
        print("negative fixture passed: generated definition drift")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate the RFC 0018 canonical header syntax wire inventory"
    )
    parser.add_argument("--schema", type=Path, default=DEFAULT_SCHEMA)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true", help="verify generated output")
    mode.add_argument("--self-test", action="store_true", help="run schema mutation tests")
    args = parser.parse_args()

    try:
        if args.self_test:
            return run_self_test(args.schema)
        if args.check:
            return run_check(args.schema, args.output)
        output = generated_text(args.schema)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(output, encoding="utf-8")
        print(f"generated {args.output}")
        return 0
    except SchemaError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
