#!/usr/bin/env python3

import argparse
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RUNNER_CONFIGS = {
    Path("products/zomlang/tests/conformance/runners/ast/lit.cfg.py"): "ast",
    Path("products/zomlang/tests/conformance/runners/diagnostics/lit.cfg.py"):
        "diagnostics",
}
CMAKE_ROOT = Path("products/zomlang/tests/conformance/CMakeLists.txt")
CMAKE_RUNNERS = (
    Path("products/zomlang/tests/conformance/runners/ast/CMakeLists.txt"),
    Path("products/zomlang/tests/conformance/runners/diagnostics/CMakeLists.txt"),
)


def check_config(path: Path, text: str, layer: str) -> list[str]:
    errors: list[str] = []
    expected_root = (
        f'os.path.join(cmake_binary_dir, "lit-exec", "{layer}")'
    )
    if expected_root not in text:
        errors.append(f"{path}: lit execution root is not build-local")
    for forbidden, reason in (
        ("tempfile.mkdtemp(", "per-invocation temporary directory leak"),
        ('os.path.join(runner_root, "Output")', "source-tree execution root"),
        (
            'config.substitutions.append(("%t",',
            "custom shared temporary substitution",
        ),
    ):
        if forbidden in text:
            errors.append(f"{path}: {reason}")
    return errors


def check_cmake(files: dict[Path, str]) -> list[str]:
    errors: list[str] = []
    root_text = files.get(CMAKE_ROOT)
    if root_text is None:
        return [f"{CMAKE_ROOT}: conformance CMake configuration is missing"]

    required_root_contracts = (
        (
            "unset(LIT_EXECUTABLE CACHE)",
            "stale legacy lit cache is not removed",
        ),
        (
            "find_program(ZOM_LIT_SOURCE_EXECUTABLE NAMES lit NO_CACHE REQUIRED)",
            "lit discovery is cacheable",
        ),
        (
            'file(STRINGS "${ZOM_LIT_SOURCE_EXECUTABLE}" '
            "ZOM_LIT_LAUNCHER_FIRST_LINE LIMIT_COUNT 1)",
            "lit launcher interpreter is not inspected",
        ),
        (
            'set(ZOM_LIT_COMMAND "${ZOM_LIT_PYTHON}" '
            '"${ZOM_LIT_SOURCE_EXECUTABLE}")',
            "lit is not bound to its package interpreter",
        ),
        (
            "COMMAND ${ZOM_LIT_COMMAND} --version",
            "configured Python lit module is not probed",
        ),
    )
    for required, reason in required_root_contracts:
        if required not in root_text:
            errors.append(f"{CMAKE_ROOT}: {reason}")
    if "find_program(LIT_EXECUTABLE" in root_text:
        errors.append(f"{CMAKE_ROOT}: legacy cacheable lit discovery remains")

    for path in CMAKE_RUNNERS:
        text = files.get(path)
        if text is None:
            errors.append(f"{path}: lit CMake runner is missing")
            continue
        if "COMMAND ${ZOM_LIT_COMMAND}" not in text:
            errors.append(f"{path}: CTest does not use the configured Python lit module")
        if "find_program(LIT_EXECUTABLE" in text:
            errors.append(f"{path}: runner retains cacheable lit discovery")
        if "${LIT_EXECUTABLE}" in text:
            errors.append(f"{path}: runner retains the legacy lit cache variable")
        if "/tmp/" in text:
            errors.append(f"{path}: runner contains a temporary executable path")
    return errors


def check(files: dict[Path, str]) -> list[str]:
    errors: list[str] = []
    for path, layer in RUNNER_CONFIGS.items():
        text = files.get(path)
        if text is None:
            errors.append(f"{path}: lit configuration is missing")
            continue
        errors.extend(check_config(path, text, layer))
    errors.extend(check_cmake(files))
    return errors


def load_files() -> dict[Path, str]:
    paths = (*RUNNER_CONFIGS, CMAKE_ROOT, *CMAKE_RUNNERS)
    return {path: (ROOT / path).read_text() for path in paths}


def self_test(files: dict[Path, str]) -> list[str]:
    failures: list[str] = []
    baseline = check(files)
    if baseline:
        return [f"self-test baseline rejected: {error}" for error in baseline]

    ast_path = next(
        path for path, layer in RUNNER_CONFIGS.items() if layer == "ast"
    )
    ast_cmake_path = CMAKE_RUNNERS[0]
    mutations = (
        (
            "source-tree root",
            files[ast_path].replace(
                'os.path.join(cmake_binary_dir, "lit-exec", "ast")',
                'os.path.join(runner_root, "Output")',
            ),
        ),
        (
            "temporary directory leak",
            files[ast_path] + "\nconfig.test_exec_root = tempfile.mkdtemp()\n",
        ),
        (
            "custom temporary substitution",
            files[ast_path]
            + '\nconfig.substitutions.append(("%t", "shared-temp"))\n',
        ),
    )
    for label, mutated in mutations:
        candidate = dict(files)
        candidate[ast_path] = mutated
        if not check(candidate):
            failures.append(f"self-test mutation escaped: {label}")

    cmake_mutations = (
        (
            "missing Python lit binding",
            CMAKE_ROOT,
            files[CMAKE_ROOT].replace(
                "find_program(ZOM_LIT_SOURCE_EXECUTABLE NAMES lit NO_CACHE REQUIRED)",
                "find_program(LIT_EXECUTABLE NAMES lit REQUIRED)",
            ),
        ),
        (
            "cached lit discovery",
            CMAKE_ROOT,
            files[CMAKE_ROOT] + "\nfind_program(LIT_EXECUTABLE NAMES lit REQUIRED)\n",
        ),
        (
            "runner uses cached executable",
            ast_cmake_path,
            files[ast_cmake_path].replace(
                "COMMAND ${ZOM_LIT_COMMAND}",
                "COMMAND ${LIT_EXECUTABLE}",
            ),
        ),
    )
    for label, path, mutated in cmake_mutations:
        candidate = dict(files)
        candidate[path] = mutated
        if not check(candidate):
            failures.append(f"self-test mutation escaped: {label}")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    failures = self_test(load_files()) if args.self_test else check(load_files())
    if failures:
        for failure in failures:
            print(failure)
        return 1
    print(
        "lit execution-root self-test passed"
        if args.self_test
        else "lit execution-root check passed"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
