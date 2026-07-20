#!/usr/bin/env python3

import argparse
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONFIGS = {
    Path("products/zomlang/tests/conformance/runners/ast/lit.cfg.py"): "ast",
    Path("products/zomlang/tests/conformance/runners/diagnostics/lit.cfg.py"):
        "diagnostics",
}


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


def check(files: dict[Path, str]) -> list[str]:
    errors: list[str] = []
    for path, layer in CONFIGS.items():
        text = files.get(path)
        if text is None:
            errors.append(f"{path}: lit configuration is missing")
            continue
        errors.extend(check_config(path, text, layer))
    return errors


def load_files() -> dict[Path, str]:
    return {path: (ROOT / path).read_text() for path in CONFIGS}


def self_test(files: dict[Path, str]) -> list[str]:
    failures: list[str] = []
    baseline = check(files)
    if baseline:
        return [f"self-test baseline rejected: {error}" for error in baseline]

    ast_path = next(path for path, layer in CONFIGS.items() if layer == "ast")
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
