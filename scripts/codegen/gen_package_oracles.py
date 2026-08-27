#!/usr/bin/env python3
"""Generate and verify the checked-in RFC 0012 package oracle inventory."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "zomlang/tests/fixtures"
ORACLE_ROOT = FIXTURES / "package-oracles"
LOCK_PATH = FIXTURES / "Zom.lock.golden"
PUBGRUB_PATH = FIXTURES / "pubgrub-scenarios.json"
CATALOG_PATH = ORACLE_ROOT / "package-generated-oracles.json"
VENDOR_CHECKER = ROOT / "zomlang/tests/tools/check-vendored-dependencies.py"
PUBGRUB_CHECKER = ROOT / "zomlang/tests/tools/check-pubgrub-scenarios.py"
RUNTIME_PRODUCERS = {
    "package-resolver-test": ROOT
    / "zomlang/tests/unittests/compiler/driver/package-resolver-test.cc",
    "target-registry-test": ROOT
    / "zomlang/tests/unittests/compiler/ir/target-registry-test.cc",
    "build-script-execution-key-test": ROOT
    / "zomlang/tests/unittests/compiler/driver/build-script-execution-key-test.cc",
    "source-record-test": ROOT
    / "zomlang/tests/unittests/compiler/driver/source-record-test.cc",
    "source-tree-test": ROOT
    / "zomlang/tests/unittests/compiler/driver/source-tree-test.cc",
}


def u8(value: int) -> bytes:
    return struct.pack(">B", value)


def u16(value: int) -> bytes:
    return struct.pack(">H", value)


def u32(value: int) -> bytes:
    return struct.pack(">I", value)


def u64(value: int) -> bytes:
    return struct.pack(">Q", value)


def byte_string(value: str | bytes) -> bytes:
    encoded = value.encode("utf-8") if isinstance(value, str) else value
    return u64(len(encoded)) + encoded


def sequence(values: list[bytes]) -> bytes:
    return u64(len(values)) + b"".join(values)


def sha256(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def domain_digest(domain: str, value: bytes) -> str:
    return sha256(domain.encode("ascii") + b"\0" + value)


def canonical_path(*segments: str) -> bytes:
    return sequence([byte_string(segment) for segment in segments])


def workspace_path(parents: int, *segments: str) -> bytes:
    return u32(parents) + canonical_path(*segments)


def feature_set(*features: str) -> bytes:
    return sequence([byte_string(feature) for feature in sorted(features)])


def registry_source(url: str, trust_digest: bytes) -> bytes:
    return u8(1) + byte_string(url) + trust_digest


def vcs_source(url: str, revision: bytes, *subdirectory: str) -> bytes:
    return u8(2) + byte_string(url) + u8(1) + revision + canonical_path(*subdirectory)


def local_source(parents: int, *segments: str) -> bytes:
    return u8(3) + workspace_path(parents, *segments)


def package_key(source: bytes, name: str, version: str, *features: str) -> bytes:
    return source + byte_string(name) + byte_string(version) + feature_set(*features)


def generated_lock() -> bytes:
    registry = registry_source(
        "https://example.com/index", hashlib.sha256(b"registry trust").digest()
    )
    vcs = vcs_source(
        "https://example.com/repo.git", bytes(range(20)), "math"
    )
    local = local_source(0, "app")
    registry_key = package_key(registry, "codec", "2.0.0")
    vcs_key = package_key(vcs, "math", "1.2.3")
    local_key = package_key(local, "app", "1.0.0", "fast")
    signing_key = domain_digest("zom.ed25519-key", bytes([0x2A]) * 32)

    sections = [
        "\n".join(
            [
                "[[package]]",
                f'key = "{registry_key.hex()}"',
                'source-kind = "registry"',
                f'source-key = "{registry.hex()}"',
                'name = "codec"',
                'version = "2.0.0"',
                "features = []",
                f'manifest-sha256 = "{sha256(b"registry manifest")}"',
                f'source-tree-sha256 = "{sha256(b"registry tree")}"',
                'archive-format = "tar-zstd"',
                f'archive-sha256 = "{sha256(b"archive")}"',
                f'signing-key = "{signing_key}"',
            ]
        ),
        "\n".join(
            [
                "[[package]]",
                f'key = "{vcs_key.hex()}"',
                'source-kind = "vcs"',
                f'source-key = "{vcs.hex()}"',
                'name = "math"',
                'version = "1.2.3"',
                "features = []",
                f'manifest-sha256 = "{sha256(b"vcs manifest")}"',
                f'source-tree-sha256 = "{sha256(b"vcs tree")}"',
                "",
                "[[package.dependency]]",
                'domain = "build"',
                'alias = "codec"',
                f'target-key = "{registry_key.hex()}"',
            ]
        ),
        "\n".join(
            [
                "[[package]]",
                f'key = "{local_key.hex()}"',
                'source-kind = "local"',
                f'source-key = "{local.hex()}"',
                'name = "app"',
                'version = "1.0.0"',
                'features = ["fast"]',
                f'manifest-sha256 = "{sha256(b"local manifest")}"',
                f'source-tree-sha256 = "{sha256(b"local tree")}"',
                "",
                "[[package.dependency]]",
                'domain = "target"',
                'alias = "math"',
                f'target-key = "{vcs_key.hex()}"',
            ]
        ),
    ]
    return ("\n\n".join(sections) + "\n").encode("utf-8")


def pubgrub_document() -> dict[str, object]:
    return {
        "schema": "zom.pubgrub-scenarios",
        "scenarios": [
            {
                "id": "greatest-eligible-release",
                "root": "app@1.0.0",
                "requirements": ["math@local:../math ^1.0.0 features=fast"],
                "available": ["math@1.2.0", "app@1.0.0", "math@1.3.0"],
                "expected": {
                    "packages": ["app@1.0.0[target]", "math@1.3.0[target,fast]"],
                    "edgeCount": 1,
                    "resolutionSha256": "53522dd5c07d5622473fc856cbe89bceb4ac05164e970d73589359d199b07e99",
                },
            },
            {
                "id": "no-version-conflict",
                "root": "app@1.0.0",
                "requirements": ["math@local:../math >=2.0.0"],
                "available": ["app@1.0.0", "math@1.3.0"],
                "expected": {
                    "issue": "NoVersionSatisfiesConstraints",
                    "incompatibilityRecordCount": 3,
                    "incompatibilityGraphSha256": "2d4ed0e00ea662d12de217d317136647ad67e4a4c6630ed85b4893bad3a1e78e",
                },
            },
            {
                "id": "backtrack-highest-conflict",
                "root": "app@1.0.0",
                "requirements": [
                    "app -> solver ^1.0.0",
                    "solver@1.1.0 -> util >=2.0.0",
                    "solver@1.0.0 -> util ^1.0.0",
                ],
                "available": [
                    "solver@1.1.0",
                    "util@1.5.0",
                    "app@1.0.0",
                    "solver@1.0.0",
                ],
                "expected": {
                    "selected": ["app@1.0.0", "solver@1.0.0", "util@1.5.0"]
                },
            },
            {
                "id": "skip-yanked-release",
                "root": "app@1.0.0",
                "requirements": ["math@local:../math ^1.0.0"],
                "available": ["app@1.0.0", "math@1.2.0", "math@1.3.0[yanked]"],
                "expected": {"selected": ["app@1.0.0", "math@1.2.0"]},
            },
            {
                "id": "separate-activation-domains",
                "root": "app@1.0.0",
                "requirements": ["target:math features=fast", "build:math features=safe"],
                "available": ["math@1.0.0", "app@1.0.0"],
                "expected": {
                    "packages": [
                        "app@1.0.0[target]",
                        "math@1.0.0[target,fast]",
                        "math@1.0.0[build,safe]",
                    ],
                    "edgeCount": 2,
                },
            },
        ],
    }


K_LOAD_WORD_ABSOLUTE = 0x20
K_JUMP_EQUAL = 0x15
K_JUMP_GREATER = 0x25
K_JUMP_GREATER_EQUAL = 0x35
K_JUMP_BITS_SET = 0x45
K_RETURN = 0x06
K_ALLOW = 0x7FFF0000
K_TRAP = 0x00030000


@dataclass(frozen=True)
class Instruction:
    code: int
    jump_true: int
    jump_false: int
    operand: int

    def encode(self) -> bytes:
        return u16(self.code) + u8(self.jump_true) + u8(self.jump_false) + u32(self.operand)


def add(output: list[Instruction], code: int, jt: int, jf: int, operand: int) -> None:
    output.append(Instruction(code, jt, jf, operand))


def trap(output: list[Instruction]) -> None:
    add(output, K_RETURN, 0, 0, K_TRAP)


def allow(output: list[Instruction]) -> None:
    add(output, K_RETURN, 0, 0, K_ALLOW)


def argument_offset(index: int) -> int:
    return 16 + index * 8


def require_equal(output: list[Instruction], argument: int, value: int) -> None:
    add(output, K_LOAD_WORD_ABSOLUTE, 0, 0, argument_offset(argument))
    add(output, K_JUMP_EQUAL, 1, 0, value)
    trap(output)


def require_equal_high(output: list[Instruction], argument: int, value: int) -> None:
    add(output, K_LOAD_WORD_ABSOLUTE, 0, 0, argument_offset(argument) + 4)
    add(output, K_JUMP_EQUAL, 1, 0, value)
    trap(output)


def require_no_bits(output: list[Instruction], argument: int, bits: int) -> None:
    add(output, K_LOAD_WORD_ABSOLUTE, 0, 0, argument_offset(argument))
    add(output, K_JUMP_BITS_SET, 0, 1, bits)
    trap(output)


def require_value_or_range(
    output: list[Instruction], argument: int, value: int, minimum: int, maximum: int
) -> None:
    add(output, K_LOAD_WORD_ABSOLUTE, 0, 0, argument_offset(argument))
    add(output, K_JUMP_EQUAL, 4, 0, value)
    add(output, K_JUMP_GREATER_EQUAL, 1, 0, minimum)
    trap(output)
    add(output, K_JUMP_GREATER, 0, 1, maximum)
    trap(output)


def require_three_values(
    output: list[Instruction], argument: int, first: int, second: int, third: int
) -> None:
    add(output, K_LOAD_WORD_ABSOLUTE, 0, 0, argument_offset(argument))
    add(output, K_JUMP_EQUAL, 3, 0, first)
    add(output, K_JUMP_EQUAL, 2, 0, second)
    add(output, K_JUMP_EQUAL, 1, 0, third)
    trap(output)


def policy_block(policy: str) -> list[Instruction]:
    result: list[Instruction] = []
    if policy == "execveat":
        require_equal(result, 0, 7)
        require_equal(result, 4, 0x1000)
    elif policy == "seccomp":
        require_equal(result, 0, 1)
        require_equal(result, 1, 0)
    elif policy == "read":
        require_value_or_range(result, 0, 3, 0, 0)
    elif policy == "write":
        require_value_or_range(result, 0, 4, 0, 0)
    elif policy == "bootstrap-write":
        require_three_values(result, 0, 0, 4, 8)
    elif policy == "close":
        require_value_or_range(result, 0, 0, 3, 7)
    elif policy == "fstat":
        require_equal(result, 0, 0)
    elif policy == "mmap":
        require_no_bits(result, 2, 0x4)
        require_equal(result, 3, 0x22)
        require_equal(result, 4, 0xFFFFFFFF)
        require_equal_high(result, 4, 0xFFFFFFFF)
    elif policy == "mprotect":
        require_no_bits(result, 2, 0x4)
    elif policy == "openat2":
        require_value_or_range(result, 0, 5, 6, 6)
    elif policy != "none":
        raise ValueError(f"unknown seccomp policy: {policy}")
    allow(result)
    return result


def seccomp_filter(architecture: str, phase: str) -> bytes:
    x86 = architecture == "x86_64"
    numbers = (
        [0, 1, 3, 5, 9, 10, 11, 12, 14, 15, 60, 231, 437]
        if x86
        else [63, 64, 57, 80, 222, 226, 215, 214, 135, 139, 93, 94, 437]
    )
    policies = [
        "read",
        "write",
        "close",
        "fstat",
        "mmap",
        "mprotect",
        "none",
        "none",
        "none",
        "none",
        "none",
        "none",
        "openat2",
    ]
    rules: list[tuple[int, str]] = []
    if phase == "bootstrap":
        rules.extend([(322 if x86 else 281, "execveat"), (317 if x86 else 277, "seccomp")])
    for number, policy in zip(numbers, policies, strict=True):
        if phase == "bootstrap" and policy == "write":
            policy = "bootstrap-write"
        rules.append((number, policy))

    result: list[Instruction] = []
    add(result, K_LOAD_WORD_ABSOLUTE, 0, 0, 4)
    add(result, K_JUMP_EQUAL, 1, 0, 0xC000003E if x86 else 0xC00000B7)
    trap(result)
    add(result, K_LOAD_WORD_ABSOLUTE, 0, 0, 0)
    if x86:
        add(result, K_JUMP_BITS_SET, 0, 1, 0x40000000)
        trap(result)
    for number, policy in rules:
        block = policy_block(policy)
        add(result, K_JUMP_EQUAL, 0, len(block), number)
        result.extend(block)
    trap(result)
    return b"".join(instruction.encode() for instruction in result)


def source_tree_digest() -> tuple[bytes, str]:
    files = [
        (canonical_path("Zom.toml"), b"manifest"),
        (canonical_path("src", "main.zom"), b"main"),
    ]
    records = [path + u64(len(content)) + hashlib.sha256(content).digest() for path, content in files]
    framing = sequence(records)
    return framing, domain_digest("zom.source-tree", framing)


def codec_oracles() -> dict[str, object]:
    package_manifest = byte_string("a") + byte_string("0.0.0") + u32(2026)
    workspace = sequence([workspace_path(0, "a"), workspace_path(0, "b")])
    local_feature = u8(1) + byte_string("fast")
    dependency_feature = u8(2) + byte_string("math")
    dependency_named_feature = u8(3) + byte_string("math") + byte_string("simd")
    semver = bytes(
        [
            0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 5,
            *b"1.2.3", 1, 1, 0, 0, 0, 0, 0, 0, 0, 5, *b"2.0.0",
            0, 0, 0, 0, 0, 0, 0, 0, 0,
        ]
    )
    dependency = bytes(
        [
            0, 0, 0, 0, 0, 0, 0, 1, ord("a"), 0, 0, 0, 0, 0, 0, 0, 1,
            ord("b"), 1, 3, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
            0, 0, 0, 0, 4, *b"math", 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0,
        ]
    )
    minimal_manifest = bytes(
        [
            1, 0, 0, 0, 0, 0, 0, 0, 1, ord("a"), 0, 0, 0, 0, 0, 0, 0, 5,
            *b"0.0.0", 0, 0, 0x07, 0xEA, *([0] * 67),
        ]
    )
    source_framing, source_digest = source_tree_digest()
    named_symbol = u8(2) + byte_string("x")
    records = sequence([byte_string(b"\xA1")])

    values: list[tuple[str, bytes, str | None]] = [
        ("package-manifest", package_manifest, None),
        ("workspace-manifest", workspace, None),
        ("feature-edge-local", local_feature, None),
        ("feature-edge-dependency", dependency_feature, None),
        ("feature-edge-dependency-feature", dependency_named_feature, None),
        ("semver-constraint", semver, None),
        ("dependency-requirement", dependency, None),
        ("minimal-canonical-manifest", minimal_manifest, None),
        ("source-tree-framing", source_framing, source_digest),
        ("trusted-runtime-symbol-name", named_symbol, domain_digest("zom.build-runtime-symbol-name", named_symbol)),
    ]
    result: dict[str, object] = {
        name: {
            "byteLength": len(value),
            "hex": value.hex(),
            "sha256": sha256(value),
            **({"domainSha256": domain_hash} if domain_hash is not None else {}),
        }
        for name, value, domain_hash in values
    }
    for kind, domain in [
        ("symbols", "zom.build-runtime-symbols"),
        ("relocations", "zom.build-runtime-relocations"),
        ("operations", "zom.build-runtime-operations"),
    ]:
        result[f"trusted-runtime-{kind}"] = {
            "framingHex": records.hex(),
            "domainSha256": domain_digest(domain, records),
        }
    result["vcs-selector-tag"] = {
        "framingHex": (u8(2) + byte_string("release-1")).hex(),
        "domainSha256": domain_digest("zom.vcs-selector", u8(2) + byte_string("release-1")),
    }
    result["ed25519-signing-key-zero"] = {
        "publicKeyHex": bytes(32).hex(),
        "domainSha256": domain_digest("zom.ed25519-key", bytes(32)),
    }
    return {"schema": "zom.package-codec-oracles", "oracles": result}


def json_bytes(document: object) -> bytes:
    return (json.dumps(document, indent=2, ensure_ascii=True) + "\n").encode("utf-8")


def generated_outputs() -> dict[Path, bytes]:
    outputs: dict[Path, bytes] = {
        LOCK_PATH: generated_lock(),
        PUBGRUB_PATH: json_bytes(pubgrub_document()),
        ORACLE_ROOT / "package-codec-oracles.json": json_bytes(codec_oracles()),
    }
    for architecture in ["x86_64", "aarch64"]:
        for phase in ["bootstrap", "runtime"]:
            outputs[ORACLE_ROOT / f"linux-seccomp-{architecture}-{phase}.bpf"] = seccomp_filter(
                architecture, phase
            )
    return outputs


def catalog(outputs: dict[Path, bytes]) -> bytes:
    managed = [
        {
            "path": str(path.relative_to(ROOT)),
            "byteLength": len(value),
            "sha256": sha256(value),
            "producer": "scripts/codegen/gen_package_oracles.py",
        }
        for path, value in sorted(outputs.items(), key=lambda item: str(item[0]))
    ]
    document = {
        "schema": "zom.package-generated-oracles",
        "managed": managed,
        "delegated": [
            {
                "path": "zomlang/compiler/driver/package/vendor/vendor-manifest.json",
                "producer": "zomlang/tests/tools/check-vendored-dependencies.py --write",
                "checker": "zomlang/tests/tools/check-vendored-dependencies.py --check",
            },
            {
                "path": "build/generated/package-resolver-performance-edges.bin",
                "producer": "scripts/codegen/gen_package_resolver_performance_edges.py --packages 10000 --edges 50000",
                "checker": "performance-package-resolver",
                "checkedIn": False,
                "sha256": "2564b53511aa1bf693654f0272a7d56201211ddcd6596885958858ecfe5432ed",
                "reason": "The 400008-byte performance payload is build output with a fixed generator input and executable SHA-256 check.",
            },
            {
                "path": "build/**/zom-linux-sandbox-runtime-fixture",
                "producer": "zom-linux-sandbox-runtime-fixture CMake target",
                "checker": "linux-native-sandbox-integration",
                "checkedIn": False,
                "reason": "The static PIE image is target-toolchain output and is admitted by digest and ELF facts at execution time.",
            },
        ],
        "runtimeProduced": [
            {
                "artifact": "ResolutionOutput canonical bytes",
                "producer": "package-resolver-test",
                "test": "PackageResolverTest.SelectsGreatestEligibleReleaseAndEmitsGraph",
                "sha256": "53522dd5c07d5622473fc856cbe89bceb4ac05164e970d73589359d199b07e99",
            },
            {
                "artifact": "resolver incompatibility derivation graph",
                "producer": "package-resolver-test",
                "test": "PackageResolverTest.ProducesCanonicalConflictExplanation",
                "sha256": "2d4ed0e00ea662d12de217d317136647ad67e4a4c6630ed85b4893bad3a1e78e",
            },
            {
                "artifact": "registered target selection and registry revision",
                "producer": "target-registry-test",
                "test": "Target registry issues and verifies one revision-bound package selection",
                "sha256": "c469c35f6f5a8258d48e965149c189c9a859201f6e4db6d80e230b47a8891f28",
            },
            {
                "artifact": "build execution key",
                "producer": "build-script-execution-key-test",
                "test": "BuildScriptExecutionKey canonicalizes the complete host closure",
                "sha256": "be3bd2d50289ca57e89c6cef56378ee97595261fd63101fe2f87c9dba2722a36",
            },
            {
                "artifact": "build script output record",
                "producer": "build-script-execution-key-test",
                "test": "Build-script cache miss publishes one complete byte-identical output record",
                "sha256": "07bb991bb4c5d57e573c2de43ea6612ffe5003f247420a9239cb8e454c614f96",
            },
            {
                "artifact": "registry signed release and source snapshot records",
                "producer": "source-record-test",
                "test": "SourceRecordTest.VerifiesCompleteRegistryReleaseSignature",
                "sha256": "9bdffe9b4d599f8da53af4a974bf59bba1bd5a1290d994c06a0e5a01999d53b0",
            },
            {
                "artifact": "source tree framing and digest",
                "producer": "source-tree-test",
                "test": "SourceTreeTest.SortsInventoryAndProducesPermutationInvariantDigest",
                "sha256": "d9561490cd6762984ced6d62ec14b571808135bf8ac786bac1aeae0b2375e717",
            },
        ],
    }
    return json_bytes(document)


def expected_outputs() -> dict[Path, bytes]:
    outputs = expected_outputs_without_catalog()
    outputs[CATALOG_PATH] = catalog(outputs)
    return outputs


def expected_outputs_without_catalog() -> dict[Path, bytes]:
    return generated_outputs()


def atomic_write(path: Path, value: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_bytes(value)
    os.replace(temporary, path)


def check_outputs(outputs: dict[Path, bytes]) -> list[str]:
    errors: list[str] = []
    expected_paths = set(outputs)
    if ORACLE_ROOT.exists():
        actual_oracles = {path for path in ORACLE_ROOT.rglob("*") if path.is_file()}
        for extra in sorted(actual_oracles - expected_paths):
            errors.append(f"undeclared generated package oracle: {extra.relative_to(ROOT)}")
    for path, expected in outputs.items():
        if not path.is_file():
            errors.append(f"missing generated package oracle: {path.relative_to(ROOT)}")
            continue
        actual = path.read_bytes()
        if actual != expected:
            errors.append(f"stale generated package oracle: {path.relative_to(ROOT)}")
    return errors


def check_delegated_vendor() -> list[str]:
    completed = subprocess.run(
        ["python3", str(VENDOR_CHECKER), "--check"],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    if completed.returncode == 0:
        return []
    detail = completed.stderr.strip() or completed.stdout.strip() or "unknown failure"
    return [f"delegated vendored manifest check failed: {detail}"]


def check_pubgrub_replay() -> list[str]:
    completed = subprocess.run(
        [
            "python3",
            str(PUBGRUB_CHECKER),
            "--corpus",
            str(PUBGRUB_PATH),
            "--replay-source",
            str(RUNTIME_PRODUCERS["package-resolver-test"]),
        ],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    if completed.returncode == 0:
        return []
    detail = completed.stderr.strip() or completed.stdout.strip() or "unknown failure"
    return [f"PubGrub replay oracle check failed: {detail}"]


def check_runtime_producers() -> list[str]:
    document = json.loads(catalog(expected_outputs_without_catalog()).decode("utf-8"))
    errors: list[str] = []
    for artifact in document["runtimeProduced"]:
        producer = artifact["producer"]
        source_path = RUNTIME_PRODUCERS.get(producer)
        if source_path is None or not source_path.is_file():
            errors.append(f"unknown runtime oracle producer: {producer}")
            continue
        source = source_path.read_text(encoding="utf-8")
        test_marker = f'ZC_TEST("{artifact["test"]}")'
        if test_marker not in source:
            errors.append(
                f"runtime oracle producer {producer} is missing test {artifact['test']}"
            )
        digest = artifact.get("sha256")
        if digest is not None and digest not in source:
            errors.append(
                f"runtime oracle producer {producer} is missing digest {digest}"
            )
    return errors


def write() -> None:
    completed = subprocess.run(
        ["python3", str(VENDOR_CHECKER), "--write"], cwd=ROOT, check=False
    )
    if completed.returncode != 0:
        raise SystemExit("delegated vendored manifest regeneration failed")
    for path, value in expected_outputs().items():
        atomic_write(path, value)


def self_test() -> None:
    outputs = expected_outputs()
    with tempfile.TemporaryDirectory(prefix="zom-package-oracles-") as temporary:
        root = Path(temporary)
        remapped = {root / path.relative_to(ROOT): value for path, value in outputs.items()}
        for path, value in remapped.items():
            atomic_write(path, value)
        for path in sorted(remapped):
            original = path.read_bytes()
            path.write_bytes(original + b"\0")
            if not check_remapped(remapped):
                raise SystemExit(f"mutation was not rejected: {path.relative_to(root)}")
            path.write_bytes(original)
        extra = root / ORACLE_ROOT.relative_to(ROOT) / "undeclared.bin"
        extra.write_bytes(b"unexpected")
        if not check_remapped(remapped):
            raise SystemExit("undeclared oracle mutation was not rejected")
    print(f"RFC 0012 generated-oracle mutation checks passed ({len(outputs) + 1} cases)")


def check_remapped(outputs: dict[Path, bytes]) -> bool:
    oracle_roots = [path.parent for path in outputs if path.name == CATALOG_PATH.name]
    errors = []
    for path, expected in outputs.items():
        if not path.is_file() or path.read_bytes() != expected:
            errors.append(str(path))
    for oracle_root in oracle_roots:
        actual = {path for path in oracle_root.rglob("*") if path.is_file()}
        expected = {path for path in outputs if path.is_relative_to(oracle_root)}
        errors.extend(str(path) for path in actual - expected)
    return bool(errors)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--write", action="store_true", help="regenerate every checked-in oracle")
    mode.add_argument("--check", action="store_true", help="fail if any oracle is stale")
    mode.add_argument("--self-test", action="store_true", help="prove drift detection by mutation")
    arguments = parser.parse_args()

    if arguments.write:
        write()
        print(f"generated {len(expected_outputs())} RFC 0012 package oracle files")
        return
    if arguments.self_test:
        self_test()
        return

    errors = (
        check_outputs(expected_outputs())
        + check_delegated_vendor()
        + check_pubgrub_replay()
        + check_runtime_producers()
    )
    if errors:
        for error in errors:
            print(f"error: {error}")
        raise SystemExit(1)
    print(f"RFC 0012 generated-oracle check passed ({len(expected_outputs())} files)")


if __name__ == "__main__":
    main()
