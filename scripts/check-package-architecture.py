#!/usr/bin/env python3
"""Enforce the repository-level architecture gates required by RFC 0012."""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from collections.abc import Callable
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

CLI_SOURCE = Path("utils/zomc/zomc.cc")
REQUEST_HEADER = Path(
    "compiler/driver/package/package-compilation-request.h"
)
REQUEST_SOURCE = Path(
    "compiler/driver/package/package-compilation-request.cc"
)
DRIVER_CMAKE = Path("compiler/driver/CMakeLists.txt")
PACKAGE_CMAKE = Path("compiler/driver/package/CMakeLists.txt")
VENDOR_CMAKE = Path("compiler/driver/package/vendor/CMakeLists.txt")
OPTIONS_CMAKE = Path("cmake/utils/options.cmake")
SANDBOX_TEST_CMAKE = Path(
    "tests/unittests/compiler/driver/CMakeLists.txt"
)
CI_WORKFLOW = Path(".github/workflows/CI.yml")
SANDBOX_RUNNER = Path("scripts/run-linux-sandbox-integration.sh")
TESTS_CMAKE = Path("tests/CMakeLists.txt")
PERFORMANCE_CMAKE = Path("tests/performance/CMakeLists.txt")
PERFORMANCE_RUNNER = Path("scripts/run_package_resolver_performance.py")
PERFORMANCE_SOURCE = Path(
    "tests/performance/package-resolver-performance.cc"
)
RESOLVER_HEADER = Path(
    "compiler/driver/package/package-resolver.h"
)
RESOLVER_SOURCE = Path(
    "compiler/driver/package/package-resolver.cc"
)
FEATURE_RESOLVER_SOURCE = Path(
    "compiler/driver/package/feature-resolver.cc"
)
LOCKFILE_SOURCE = Path("compiler/driver/package/lockfile.cc")
PACKAGE_ORACLE_GENERATOR = Path("scripts/codegen/gen_package_oracles.py")
CONFORMANCE_CMAKE = Path("tests/conformance/CMakeLists.txt")
CMAKE_PRESETS = Path("CMakePresets.json")
SESSION_HEADER = Path("compiler/driver/session/compiler-session.h")
SESSION_SOURCE = Path("compiler/driver/session/compiler-session.cc")

REQUIRED_FILES = (
    CLI_SOURCE,
    REQUEST_HEADER,
    REQUEST_SOURCE,
    DRIVER_CMAKE,
    PACKAGE_CMAKE,
    VENDOR_CMAKE,
    OPTIONS_CMAKE,
    SANDBOX_TEST_CMAKE,
    CI_WORKFLOW,
    SANDBOX_RUNNER,
    TESTS_CMAKE,
    PERFORMANCE_CMAKE,
    PERFORMANCE_RUNNER,
    PERFORMANCE_SOURCE,
    RESOLVER_HEADER,
    RESOLVER_SOURCE,
    FEATURE_RESOLVER_SOURCE,
    LOCKFILE_SOURCE,
    PACKAGE_ORACLE_GENERATOR,
    CONFORMANCE_CMAKE,
    CMAKE_PRESETS,
    SESSION_HEADER,
    SESSION_SOURCE,
)

OLD_INSTALL_APIS = (
    "installPackageCompilationRequest",
    "installVerifiedTargetSelections",
    "installResolvedPackageGraph",
)

SYSTEM_DISCOVERY = re.compile(
    r"\b(?:find_package|find_library|find_path|find_file|pkg_check_modules|"
    r"fetchcontent_(?:declare|makeavailable)|externalproject_add|"
    r"check_include_file|check_library_exists)\s*\(",
    re.IGNORECASE,
)


def production_sources() -> dict[Path, str]:
    files: dict[Path, str] = {}
    for path in REQUIRED_FILES:
        files[path] = (ROOT / path).read_text(encoding="utf-8")

    product_roots = [
        ROOT / name
        for name in ("compiler", "runtime", "tools", "tests", "utils")
    ]
    vendor_root = (
        ROOT / "compiler" / "driver" / "package" / "vendor"
    )
    excluded_directory_names = {".antlr_build", "Output", "Testing", "__pycache__"}
    for product_root in product_roots:
        for directory, child_directories, filenames in os.walk(product_root):
            directory_path = Path(directory)
            child_directories[:] = [
                child
                for child in child_directories
                if child not in excluded_directory_names
                and directory_path / child != vendor_root
            ]
            for filename in filenames:
                path = directory_path / filename
                if path.suffix not in {".h", ".cc"}:
                    continue
                relative = path.relative_to(ROOT)
                files.setdefault(relative, path.read_text(encoding="utf-8"))
    return files


def strip_cpp_comments_and_literals(text: str) -> str:
    output: list[str] = []
    index = 0
    state = "code"
    quote = ""
    while index < len(text):
        current = text[index]
        following = text[index + 1] if index + 1 < len(text) else ""
        if state == "code":
            if current == "/" and following == "/":
                output.extend("  ")
                index += 2
                state = "line-comment"
                continue
            if current == "/" and following == "*":
                output.extend("  ")
                index += 2
                state = "block-comment"
                continue
            if current in {'"', "'"}:
                output.append(" ")
                quote = current
                index += 1
                state = "literal"
                continue
            output.append(current)
            index += 1
            continue
        if state == "line-comment":
            if current == "\n":
                output.append("\n")
                state = "code"
            else:
                output.append(" ")
            index += 1
            continue
        if state == "block-comment":
            if current == "*" and following == "/":
                output.extend("  ")
                index += 2
                state = "code"
            else:
                output.append("\n" if current == "\n" else " ")
                index += 1
            continue
        if current == "\\" and following:
            output.extend("  ")
            index += 2
        elif current == quote:
            output.append(" ")
            index += 1
            state = "code"
        else:
            output.append("\n" if current == "\n" else " ")
            index += 1
    return "".join(output)


def function_body(text: str, signature: str) -> str:
    start = text.find(signature)
    if start < 0:
        return ""
    open_brace = text.find("{", start)
    if open_brace < 0:
        return ""
    depth = 0
    for index in range(open_brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[open_brace + 1 : index]
    return ""


def function_bodies(text: str, signature: str) -> list[str]:
    bodies: list[str] = []
    cursor = 0
    while True:
        start = text.find(signature, cursor)
        if start < 0:
            return bodies
        open_brace = text.find("{", start)
        semicolon = text.find(";", start)
        if open_brace < 0:
            return bodies
        if 0 <= semicolon < open_brace:
            cursor = semicolon + 1
            continue
        depth = 0
        for index in range(open_brace, len(text)):
            if text[index] == "{":
                depth += 1
            elif text[index] == "}":
                depth -= 1
                if depth == 0:
                    bodies.append(text[open_brace + 1 : index])
                    cursor = index + 1
                    break
        else:
            return bodies


def require_markers(
    files: dict[Path, str],
    path: Path,
    markers: tuple[str, ...],
    label: str,
    errors: list[str],
) -> None:
    text = files.get(path, "")
    for marker in markers:
        if marker not in text:
            errors.append(f"{path}: missing {label} marker: {marker}")


def check_package_only_cli(files: dict[Path, str], errors: list[str]) -> None:
    require_markers(
        files,
        CLI_SOURCE,
        (
            '.addSubCommand("compile"',
            '.addOptionWithArg({"manifest-path"}',
            '.addOptionWithArg({"package"}',
            '.addOption({"lib"}',
            '.addOptionWithArg({"bin"}',
            '.expectZeroOrMoreArgs("<source>", ZC_BIND_METHOD(*this, rejectPositionalSource))',
            "packageRequest.positionalArguments.add(",
            "session->addVerifiedPackageRoot(",
        ),
        "package-only CLI",
        errors,
    )
    require_markers(
        files,
        REQUEST_HEADER,
        ("PositionalSourceArgument", "zc::Vector<zc::String> positionalArguments;"),
        "positional-source rejection",
        errors,
    )
    require_markers(
        files,
        REQUEST_SOURCE,
        (
            "if (raw.positionalArguments.size() != 0) {",
            "return InvocationIssue::PositionalSourceArgument;",
        ),
        "positional-source rejection",
        errors,
    )
    cli_code = strip_cpp_comments_and_literals(files.get(CLI_SOURCE, ""))
    if re.search(r"\bsession\s*->\s*addSourceFile\s*\(", cli_code):
        errors.append(
            f"{CLI_SOURCE}: positional direct-source installation is forbidden"
        )
    if re.search(r"\bsession\s*->\s*addPackageSourceFile\s*\(", cli_code):
        errors.append(f"{CLI_SOURCE}: package source path reread is forbidden")
    if re.search(
        r"\.expect(?:One|OneOrMore)Args\s*\(\s*\"<source>\"", files.get(CLI_SOURCE, "")
    ):
        errors.append(
            f"{CLI_SOURCE}: positional direct-source compatibility is forbidden"
        )


def check_vendored_only_dependencies(files: dict[Path, str], errors: list[str]) -> None:
    require_markers(
        files,
        DRIVER_CMAKE,
        (
            "add_subdirectory(package/vendor)",
            "check-vendored-dependencies",
            "zom_vendor_archive",
            "zom_vendor_sodium",
            "zom_vendor_zstd",
        ),
        "vendored dependency",
        errors,
    )
    require_markers(
        files,
        PACKAGE_CMAKE,
        (
            "PUBLIC zom_vendor_archive",
            "zom_vendor_sodium",
            "zom_vendor_tomlplusplus",
            "zom_vendor_zstd",
        ),
        "vendored link closure",
        errors,
    )
    require_markers(
        files,
        VENDOR_CMAKE,
        (
            "add_library(zom_vendor_tomlplusplus INTERFACE)",
            "add_library(zom_vendor_archive STATIC",
            "archive_read_support_filter_none.c",
            "archive_read_support_format_tar.c",
            "add_library(zom_vendor_zstd STATIC",
            "add_library(zom_vendor_sodium STATIC",
        ),
        "direct vendor source inventory",
        errors,
    )
    for path in (DRIVER_CMAKE, PACKAGE_CMAKE, VENDOR_CMAKE):
        text = re.sub(r"(?m)^\s*#.*$", "", files.get(path, ""))
        if SYSTEM_DISCOVERY.search(text):
            errors.append(f"{path}: system dependency discovery is forbidden")
    if re.search(
        r"\badd_subdirectory\s*\(", files.get(VENDOR_CMAKE, ""), re.IGNORECASE
    ):
        errors.append(
            f"{VENDOR_CMAKE}: upstream dependency build systems are forbidden"
        )


def check_linux_privileged_gate(files: dict[Path, str], errors: list[str]) -> None:
    require_markers(
        files,
        OPTIONS_CMAKE,
        (
            "option(\n  ZOM_ENABLE_PRIVILEGED_LINUX_SANDBOX_TESTS",
            "requires ZOM_ENABLE_UNITTESTS=ON",
            "requires a Linux build host",
            "supports only x86-64 and AArch64",
        ),
        "privileged Linux option",
        errors,
    )
    if "SKIP_RETURN_CODE" in files.get(SANDBOX_TEST_CMAKE, ""):
        errors.append(
            f"{SANDBOX_TEST_CMAKE}: privileged CTest skip behavior is forbidden"
        )
    if "continue-on-error: true" in files.get(CI_WORKFLOW, ""):
        errors.append(f"{CI_WORKFLOW}: privileged CI failure suppression is forbidden")
    runner = files.get(SANDBOX_RUNNER, "")
    if re.search(r"(?:\|\|\s*true|\bexit\s+0\b)", runner):
        errors.append(
            f"{SANDBOX_RUNNER}: privileged runner skip or fallback is forbidden"
        )
    require_markers(
        files,
        SANDBOX_TEST_CMAKE,
        (
            "if(ZOM_ENABLE_PRIVILEGED_LINUX_SANDBOX_TESTS)",
            "add_executable(\n      package-linux-native-sandbox-integration-test",
            "NAME linux-native-sandbox-integration",
            "COMMAND $<TARGET_FILE:package-linux-native-sandbox-integration-test>",
            'LABELS "integration;package;linux;privileged;release"',
        ),
        "privileged Linux CTest",
        errors,
    )
    if "package-linux-native-sandbox-integration-test EXCLUDE_FROM_ALL" in files.get(
        SANDBOX_TEST_CMAKE, ""
    ):
        errors.append(
            f"{SANDBOX_TEST_CMAKE}: privileged integration target must enter the enabled build"
        )
    require_markers(
        files,
        CI_WORKFLOW,
        (
            "check-linux-sandbox-integration:",
            "runs-on: ubuntu-latest",
            "cmake --preset sanitizer",
            "-DZOM_ENABLE_PRIVILEGED_LINUX_SANDBOX_TESTS=ON",
            "--target package-linux-native-sandbox-integration-test",
            "scripts/run-linux-sandbox-integration.sh build-sanitizer",
        ),
        "privileged Linux CI",
        errors,
    )
    require_markers(
        files,
        SANDBOX_RUNNER,
        (
            "cgroup_root=/sys/fs/cgroup",
            'cgroup_parent="${cgroup_root}/zom-linux-sandbox-${UID}-$$"',
            'ZOM_LINUX_SANDBOX_CGROUP_PARENT="${cgroup_parent}"',
            "-R '^linux-native-sandbox-integration$'",
        ),
        "delegated cgroup runner",
        errors,
    )


def check_release_performance_gate(files: dict[Path, str], errors: list[str]) -> None:
    require_markers(
        files,
        TESTS_CMAKE,
        ("if(ZOM_ENABLE_PERFORMANCE_TESTS)", "add_subdirectory(performance)"),
        "performance option",
        errors,
    )
    require_markers(
        files,
        PERFORMANCE_CMAKE,
        (
            "add_executable(package-resolver-performance-test",
            "NAME performance-package-resolver",
            '"${ZOM_ROOT}/scripts/run_package_resolver_performance.py" --executable',
            "$<TARGET_FILE:package-resolver-performance-test>",
            "--max-rss-bytes 1073741824",
        ),
        "resolver performance CTest",
        errors,
    )
    require_markers(
        files,
        PERFORMANCE_RUNNER,
        (
            'parser.add_argument("--max-rss-bytes", type=int, required=True)',
            "resource.getrusage(resource.RUSAGE_CHILDREN)",
            'if sys.platform != "darwin":',
            "if peak_rss > arguments.max_rss_bytes:",
            "return 1",
        ),
        "peak RSS wrapper",
        errors,
    )
    try:
        presets = json.loads(files.get(CMAKE_PRESETS, ""))
    except json.JSONDecodeError as error:
        errors.append(f"{CMAKE_PRESETS}: invalid JSON: {error}")
        return
    release_configures = [
        preset
        for preset in presets.get("configurePresets", [])
        if preset.get("name") == "release"
    ]
    if (
        len(release_configures) != 1
        or release_configures[0].get("cacheVariables", {}).get("CMAKE_BUILD_TYPE")
        != "Release"
    ):
        errors.append(
            f"{CMAKE_PRESETS}: exactly one Release configure preset is required"
        )
    release_builds = [
        preset
        for preset in presets.get("buildPresets", [])
        if preset.get("name") == "release"
        and preset.get("configurePreset") == "release"
    ]
    if len(release_builds) != 1:
        errors.append(f"{CMAKE_PRESETS}: release build preset is required")
    sanitizer_configures = [
        preset
        for preset in presets.get("configurePresets", [])
        if preset.get("name") == "sanitizer"
    ]
    if (
        len(sanitizer_configures) != 1
        or sanitizer_configures[0]
        .get("cacheVariables", {})
        .get("ZOM_ENABLE_PERFORMANCE_TESTS")
        != "OFF"
    ):
        errors.append(
            f"{CMAKE_PRESETS}: sanitizer preset must disable release performance tests"
        )


def check_generated_oracle_gate(files: dict[Path, str], errors: list[str]) -> None:
    require_markers(
        files,
        PACKAGE_ORACLE_GENERATOR,
        (
            'mode.add_argument("--write"',
            'mode.add_argument("--check"',
            'mode.add_argument("--self-test"',
            '"zom.package-generated-oracles"',
            '"zom.package-codec-oracles"',
            '"ResolutionOutput canonical bytes"',
            '"registered target selection and registry revision"',
            '"build execution key"',
            '"build script output record"',
        ),
        "package generated-oracle generator",
        errors,
    )
    require_markers(
        files,
        CONFORMANCE_CMAKE,
        (
            "NAME package-generated-oracles\n",
            "gen_package_oracles.py --check",
            "NAME package-generated-oracles-negative",
            "gen_package_oracles.py --self-test",
        ),
        "package generated-oracle CTest",
        errors,
    )


def check_resolver_resource_gate(files: dict[Path, str], errors: list[str]) -> None:
    require_markers(
        files,
        RESOLVER_HEADER,
        (
            "static ResolutionResult resolve(zc::MemoryResource& resource,",
            "static ResolutionResult resolveLocked(zc::MemoryResource& resource,",
            "ResolverRelease clone(zc::MemoryResource& resource) const;",
            "ResolverRoot clone(zc::MemoryResource& resource) const;",
            "zc::Array<uint8_t> encode(zc::MemoryResource& resource) const;",
        ),
        "explicit resolver allocation boundary",
        errors,
    )
    require_markers(
        files,
        RESOLVER_SOURCE,
        (
            "zc::Vector<Selection> selections(resource);",
            "FeatureResolver::expand(resource,",
            "VerifiedLockGraph::from(resource,",
            "identity::CanonicalEncoder encoder(resource);",
            "identity::PackageName::fromCanonical(resource,",
            "release.manifest().clone(resource)",
        ),
        "resource-owned resolver implementation",
        errors,
    )
    require_markers(
        files,
        FEATURE_RESOLVER_SOURCE,
        (
            "struct ResourceFeatureAllocation final",
            "ResourceFeatureAllocation allocation(resource);",
            "return expandFeatures(allocation, manifest, domain, requested, useDefaultFeatures);",
        ),
        "resource-owned feature expansion policy",
        errors,
    )
    require_markers(
        files,
        SESSION_HEADER,
        (
            "zc::MemoryResource& getPackageResolutionMemoryResource() noexcept;",
        ),
        "session-owned resolver allocation boundary",
        errors,
    )
    require_markers(
        files,
        SESSION_SOURCE,
        (
            "zc::MemoryResource packageResolutionMemory;",
            "CompilerSession::getPackageResolutionMemoryResource() noexcept",
        ),
        "session-owned resolver allocation implementation",
        errors,
    )
    require_markers(
        files,
        CLI_SOURCE,
        (
            "auto& resolverMemory = session->getPackageResolutionMemoryResource();",
            "ResolverRelease::fromLocal(resolverMemory, recordValue)",
            "PackageResolver::resolve(resolverMemory, roots, releases)",
            "PackageResolver::resolveLocked(resolverMemory, roots, releases,",
        ),
        "session-lifetime resolver caller",
        errors,
    )
    require_markers(
        files,
        PERFORMANCE_SOURCE,
        (
            "zc::CountingMemoryResource resource(upstream);",
            "ResolverRelease::fromLocal(resource, value)",
            "PackageResolver::resolve(resource, roots, releases, metrics)",
            "observedPeak = resource.peakAllocatedBytes();",
            "observedPeak <= uint64_t{1} << 30U",
            "resource.currentAllocatedBytes() == 0",
        ),
        "resolver peak-live allocation gate",
        errors,
    )

    header = strip_cpp_comments_and_literals(files.get(RESOLVER_HEADER, ""))
    if re.search(
        r"static\s+ResolutionResult\s+resolve(?:Locked)?\s*\(\s*"
        r"zc::ArrayPtr<const\s+ResolverRoot>",
        header,
    ):
        errors.append(f"{RESOLVER_HEADER}: no-resource resolver overload is forbidden")

    session = files.get(SESSION_SOURCE, "")
    resource_position = session.find("zc::MemoryResource packageResolutionMemory;")
    graph_position = session.find("zc::Maybe<package::ResolutionOutput> packageGraph;")
    if resource_position < 0 or graph_position < 0 or resource_position > graph_position:
        errors.append(
            f"{SESSION_SOURCE}: package resolution resource must be declared before the graph"
        )

    no_resource_call = re.compile(
        r"\bPackageResolver::resolve(?:Locked)?\s*\(\s*(?:roots|releases)\s*,"
    )
    for path, raw_text in sorted(files.items()):
        if path.suffix not in {".h", ".cc"}:
            continue
        if no_resource_call.search(strip_cpp_comments_and_literals(raw_text)):
            errors.append(f"{path}: package resolver call omits the required memory resource")

    scanned_bodies: list[tuple[Path, str]] = []
    resolver_text = strip_cpp_comments_and_literals(files.get(RESOLVER_SOURCE, ""))
    resolver_bodies = function_bodies(resolver_text, "zc::MemoryResource& resource")
    if not resolver_bodies:
        errors.append(f"{RESOLVER_SOURCE}: resource-aware resolver algorithm bodies are missing")
    scanned_bodies.extend((RESOLVER_SOURCE, body) for body in resolver_bodies)
    feature_text = strip_cpp_comments_and_literals(
        files.get(FEATURE_RESOLVER_SOURCE, "")
    )
    feature_bodies = function_bodies(feature_text, "FeatureExpansionResult expandFeatures(")
    if not feature_bodies:
        errors.append(
            f"{FEATURE_RESOLVER_SOURCE}: feature expansion algorithm body is missing"
        )
    scanned_bodies.extend((FEATURE_RESOLVER_SOURCE, body) for body in feature_bodies)
    lockfile_text = strip_cpp_comments_and_literals(files.get(LOCKFILE_SOURCE, ""))
    lock_bodies = function_bodies(
        lockfile_text,
        "LockedReplayVerifier::replay(\n    zc::MemoryResource& resource",
    )
    if not lock_bodies:
        errors.append(f"{LOCKFILE_SOURCE}: resource-aware lock replay body is missing")
    scanned_bodies.extend((LOCKFILE_SOURCE, body) for body in lock_bodies)

    forbidden_algorithms = (
        (
            re.compile(r"\bzc::Vector<[^\n;]+>\s+[A-Za-z_]\w*\s*;"),
            "default Vector allocation",
        ),
        (
            re.compile(r"\bidentity::CanonicalEncoder\s+[A-Za-z_]\w*\s*;"),
            "default CanonicalEncoder allocation",
        ),
        (re.compile(r"\.clone\s*\(\s*\)"), "no-resource clone call"),
        (re.compile(r"\.encode\s*\(\s*\)"), "no-resource encode call"),
        (
            re.compile(r"\bfromCanonical\s*\((?!\s*resource\b)"),
            "no-resource canonical admission",
        ),
        (
            re.compile(
                r"\b(?:coordinateBytes|copyBytes|activationKey|releaseLookupBytes|"
                r"requirementLookupBytes|buildReleaseGroups|failure|conflictFailure|"
                r"sortedFeatures|packageKey|packageBaseKey|canonicalSort|"
                r"edgeFactBytes|detectDependencyCycle|buildResolutionParts)\s*\("
                r"(?!\s*resource\b)"
            ),
            "resolver helper call omits the memory resource",
        ),
    )
    for path, body in scanned_bodies:
        for pattern, description in forbidden_algorithms:
            if pattern.search(body):
                errors.append(f"{path}: resolver allocation fallback: {description}")


def check_atomic_session_handoff(files: dict[Path, str], errors: list[str]) -> None:
    require_markers(
        files,
        SESSION_HEADER,
        (
            "class VerifiedPackageSessionInput final",
            "static zc::Maybe<VerifiedPackageSessionInput> from(",
            "friend class CompilerSession;",
            "bool installVerifiedPackageInput(VerifiedPackageSessionInput&& input);",
        ),
        "atomic package-session boundary",
        errors,
    )
    require_markers(
        files,
        SESSION_SOURCE,
        (
            "zc::Maybe<VerifiedPackageSessionInput> VerifiedPackageSessionInput::from(",
            "bool CompilerSession::installVerifiedPackageInput(VerifiedPackageSessionInput&& input)",
            "impl->packageRequest = zc::mv(input.impl->request);",
            "impl->verifiedHostTarget = zc::mv(input.impl->hostTarget);",
            "impl->verifiedTarget = zc::mv(input.impl->target);",
            "impl->packageGraph = zc::mv(input.impl->graph);",
            "impl->buildScriptPlan = zc::mv(input.impl->buildScriptPlan);",
            "impl->crateGraph = zc::mv(graph);",
            "impl->packageSnapshots = zc::mv(input.impl->snapshots);",
            "VerifiedPreparatoryCrateGraph::buildPlan(request, graph)",
            "VerifiedCrateGraph::buildFinal(input.impl->request, input.impl->graph,",
        ),
        "atomic package-session implementation",
        errors,
    )
    require_markers(
        files,
        CLI_SOURCE,
        (
            "driver::VerifiedPackageSessionInput::from(",
            "session->installVerifiedPackageInput(zc::mv(input))",
        ),
        "atomic package-session caller",
        errors,
    )
    install_body = function_body(
        files.get(SESSION_SOURCE, ""),
        "bool CompilerSession::installVerifiedPackageInput(VerifiedPackageSessionInput&& input)",
    )
    if not install_body:
        errors.append(
            f"{SESSION_SOURCE}: atomic package-session install body is missing"
        )
    else:
        first_mutation = install_body.find("impl->packageRequest =")
        crate_graph_gate = install_body.find("VerifiedCrateGraph::buildFinal(")
        rejected_graph = install_body.find(
            "if (!graphResult.is<VerifiedCrateGraph>()) { return false; }"
        )
        if (
            first_mutation < 0
            or crate_graph_gate < 0
            or rejected_graph < 0
            or crate_graph_gate > first_mutation
            or rejected_graph > first_mutation
        ):
            errors.append(
                f"{SESSION_SOURCE}: package-session validation must complete before state mutation"
            )
        elif "return false" in install_body[first_mutation:]:
            errors.append(
                f"{SESSION_SOURCE}: package-session install may fail after state mutation"
            )
    for path, raw_text in sorted(files.items()):
        if path.suffix not in {".h", ".cc"}:
            continue
        candidates = [old_api for old_api in OLD_INSTALL_APIS if old_api in raw_text]
        if not candidates:
            continue
        text = strip_cpp_comments_and_literals(raw_text)
        for old_api in candidates:
            if re.search(rf"\b{re.escape(old_api)}\b", text):
                errors.append(
                    f"{path}: forbidden split package install API remains: {old_api}"
                )


def analyze(files: dict[Path, str]) -> list[str]:
    errors: list[str] = []
    check_package_only_cli(files, errors)
    check_vendored_only_dependencies(files, errors)
    check_linux_privileged_gate(files, errors)
    check_release_performance_gate(files, errors)
    check_generated_oracle_gate(files, errors)
    check_resolver_resource_gate(files, errors)
    check_atomic_session_handoff(files, errors)
    return errors


def run_check() -> int:
    errors = analyze(production_sources())
    if errors:
        print("RFC 0012 package architecture check failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    print(
        "RFC 0012 package architecture check passed "
        "(CLI, vendor, sandbox, performance, generated-oracle, resolver-resource, "
        "and atomic session gates)."
    )
    return 0


Mutation = Callable[[dict[Path, str]], None]


def expect_rejection(
    baseline: dict[Path, str], name: str, mutate: Mutation, expected_fragment: str
) -> list[str]:
    fixture = dict(baseline)
    mutate(fixture)
    errors = analyze(fixture)
    if any(expected_fragment in error for error in errors):
        return []
    return [f"negative fixture {name!r} was not rejected for {expected_fragment!r}"]


def remove_marker(path: Path, marker: str) -> Mutation:
    def mutate(files: dict[Path, str]) -> None:
        files[path] = files[path].replace(marker, "", 1)

    return mutate


def append_text(path: Path, text: str) -> Mutation:
    def mutate(files: dict[Path, str]) -> None:
        files[path] += text

    return mutate


def replace_marker(path: Path, marker: str, replacement: str) -> Mutation:
    def mutate(files: dict[Path, str]) -> None:
        files[path] = files[path].replace(marker, replacement, 1)

    return mutate


def run_self_test() -> int:
    baseline = production_sources()
    baseline_errors = analyze(baseline)
    if baseline_errors:
        print(
            "RFC 0012 package architecture self-test baseline failed:", file=sys.stderr
        )
        for error in baseline_errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    failures: list[str] = []
    fixtures = (
        (
            "positional direct-source compatibility",
            remove_marker(
                REQUEST_SOURCE, "return InvocationIssue::PositionalSourceArgument;"
            ),
            "missing positional-source rejection marker",
        ),
        (
            "system Zstandard discovery",
            append_text(VENDOR_CMAKE, "\nfind_package(ZSTD REQUIRED)\n"),
            "system dependency discovery is forbidden",
        ),
        (
            "privileged CTest registration removed",
            remove_marker(SANDBOX_TEST_CMAKE, "NAME linux-native-sandbox-integration"),
            "missing privileged Linux CTest marker",
        ),
        (
            "privileged sanitizer CI disabled",
            remove_marker(
                CI_WORKFLOW, "-DZOM_ENABLE_PRIVILEGED_LINUX_SANDBOX_TESTS=ON"
            ),
            "missing privileged Linux CI marker",
        ),
        (
            "resolver RSS limit removed",
            remove_marker(PERFORMANCE_CMAKE, "--max-rss-bytes 1073741824"),
            "missing resolver performance CTest marker",
        ),
        (
            "generated-oracle CTest removed",
            remove_marker(CONFORMANCE_CMAKE, "NAME package-generated-oracles\n"),
            "missing package generated-oracle CTest marker",
        ),
        (
            "resolver local allocation fallback restored",
            replace_marker(
                RESOLVER_SOURCE,
                "zc::Vector<Selection> selections(resource);",
                "zc::Vector<Selection> selections;",
            ),
            "resolver allocation fallback: default Vector allocation",
        ),
        (
            "resolver encoder allocation fallback restored",
            replace_marker(
                RESOLVER_SOURCE,
                "identity::CanonicalEncoder encoder(resource);",
                "identity::CanonicalEncoder encoder;",
            ),
            "resolver allocation fallback: default CanonicalEncoder allocation",
        ),
        (
            "resolver clone allocation fallback restored",
            replace_marker(
                RESOLVER_SOURCE,
                "release.manifest().clone(resource)",
                "release.manifest().clone()",
            ),
            "resolver allocation fallback: no-resource clone call",
        ),
        (
            "resolver canonical admission fallback restored",
            replace_marker(
                RESOLVER_SOURCE,
                "identity::PackageName::fromCanonical(resource, base.name())",
                "identity::PackageName::fromCanonical(base.name())",
            ),
            "resolver allocation fallback: no-resource canonical admission",
        ),
        (
            "resolver lock encoding fallback restored",
            replace_marker(
                RESOLVER_SOURCE,
                "locked.encode(resource)",
                "locked.encode()",
            ),
            "resolver allocation fallback: no-resource encode call",
        ),
        (
            "feature resolver resource policy removed",
            replace_marker(
                FEATURE_RESOLVER_SOURCE,
                "ResourceFeatureAllocation allocation(resource);",
                "DefaultFeatureAllocation allocation;",
            ),
            "missing resource-owned feature expansion policy marker",
        ),
        (
            "resolver peak-live zero check removed",
            remove_marker(PERFORMANCE_SOURCE, "resource.currentAllocatedBytes() == 0"),
            "missing resolver peak-live allocation gate marker",
        ),
        (
            "non-release performance preset",
            remove_marker(CMAKE_PRESETS, '"CMAKE_BUILD_TYPE": "Release"'),
            "exactly one Release configure preset is required",
        ),
        (
            "sanitizer performance benchmark enabled",
            replace_marker(
                CMAKE_PRESETS,
                '"ZOM_ENABLE_PERFORMANCE_TESTS": "OFF"',
                '"ZOM_ENABLE_PERFORMANCE_TESTS": "ON"',
            ),
            "sanitizer preset must disable release performance tests",
        ),
        (
            "atomic package input factory removed",
            remove_marker(
                SESSION_HEADER, "static zc::Maybe<VerifiedPackageSessionInput> from("
            ),
            "missing atomic package-session boundary marker",
        ),
        (
            "authoritative build plan derivation removed",
            remove_marker(
                SESSION_SOURCE, "VerifiedPreparatoryCrateGraph::buildPlan(request, graph)"
            ),
            "missing atomic package-session implementation marker",
        ),
        (
            "partial package install failure",
            replace_marker(
                SESSION_SOURCE,
                "impl->packageRequest = zc::mv(input.impl->request);",
                "impl->packageRequest = zc::mv(input.impl->request);\n  return false;",
            ),
            "package-session install may fail after state mutation",
        ),
        (
            "split package install API restored",
            append_text(
                SESSION_HEADER,
                "\nbool installPackageCompilationRequest(package::Request&& request);\n",
            ),
            "forbidden split package install API remains",
        ),
    )
    for name, mutate, expected in fixtures:
        failures += expect_rejection(baseline, name, mutate, expected)

    if failures:
        print("RFC 0012 package architecture self-test failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print(
        f"RFC 0012 package architecture negative fixtures passed ({len(fixtures)}/{len(fixtures)})."
    )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()
    return run_check() if arguments.check else run_self_test()


if __name__ == "__main__":
    raise SystemExit(main())
