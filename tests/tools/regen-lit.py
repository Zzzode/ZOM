#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Snapshot regeneration tool for ZomLang conformance tests.

This tool regenerates CHECK comments in AST and diagnostics expectation files
by running the zomc compiler on the matching source under conformance/corpus.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import List, Optional, Tuple


class SnapshotRegenerator:
    """Tool to regenerate test snapshots for ZomLang conformance tests."""

    REGEN_SKIP_PREFIX = "// REGEN-SKIP:"

    def __init__(
        self, zomc_path: str, cmake_binary_dir: Optional[str], preset: Optional[str]
    ):
        self.zomc_path = zomc_path
        self.cmake_binary_dir = cmake_binary_dir
        self.preset = preset
        self.repo_root = Path(__file__).resolve().parents[2]
        self.conformance_root = (
            self.repo_root / "tests" / "conformance"
        )
        self.corpus_root = self.conformance_root / "corpus"
        self.ast_expectation_root = self.conformance_root / "expectations" / "ast"
        self.diagnostics_expectation_root = (
            self.conformance_root / "expectations" / "diagnostics"
        )

    def _zomc_path_in_build_dir(self, build_dir: Path) -> Path:
        executable = "zomc.exe" if os.name == "nt" else "zomc"
        return build_dir / "bin" / executable

    def find_zomc(self) -> Optional[str]:
        """Find zomc compiler in build directories."""
        if self.zomc_path and os.path.exists(self.zomc_path):
            return self.zomc_path

        env_cmake_binary_dir = os.environ.get("CMAKE_BINARY_DIR", "").strip()
        cmake_binary_dir = (self.cmake_binary_dir or env_cmake_binary_dir).strip()
        if cmake_binary_dir:
            potential_path = self._zomc_path_in_build_dir(Path(cmake_binary_dir))
            if potential_path.exists():
                return str(potential_path)

        zomc_in_path = shutil.which("zomc")
        if zomc_in_path:
            return zomc_in_path

        if self.preset:
            candidates = [f"build-{self.preset}"]
        else:
            candidates = [
                "build-sanitizer",
                "build-debug",
                "build-release",
                "build-coverage",
                "build",
            ]

        for build_dir in candidates:
            potential_path = self._zomc_path_in_build_dir(self.repo_root / build_dir)
            if potential_path.exists():
                return str(potential_path)

        cwd = Path.cwd()
        for build_dir in candidates:
            potential_path = self._zomc_path_in_build_dir(cwd / build_dir)
            if potential_path.exists():
                return str(potential_path)

        return None

    def run_zomc(self, test_file: str, snapshot_kind: str) -> Tuple[int, str]:
        """Run zomc and return the stream consumed by the generated RUN line."""
        zomc = self.find_zomc()
        if not zomc:
            raise RuntimeError("Could not find zomc compiler")
        package_runner = (
            self.repo_root
            / "tests"
            / "tools"
            / "run-zomc-package.py"
        )
        command_prefix = [sys.executable, str(package_runner), "--zomc", zomc]

        if snapshot_kind == "diagnostics":
            source_file = Path(test_file).resolve()
            rel = source_file.relative_to(self.corpus_root).as_posix()
            result = subprocess.run(
                [*command_prefix, "compile", "--check", rel],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                cwd=self.corpus_root,
                check=False,
            )
            return result.returncode, result.stdout

        result = subprocess.run(
            [*command_prefix, "compile", "--dump-ast", test_file],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
        )
        if result.returncode == 0:
            return result.returncode, result.stdout
        return result.returncode, result.stdout + result.stderr

    def format_json_for_check(self, json_str: str) -> List[str]:
        """Format JSON string as CHECK comments."""
        lines = []
        json_lines = json_str.strip().split("\n")

        for i, line in enumerate(json_lines):
            # Handle any JSON field with path values - detect any path and keep only filename
            # Match any JSON key with a string value that contains path separators
            match = re.match(
                r'^(\s*)"([^"]+)":\s*"([^"]*[/\\\\])([^/\\\\"]+)"(,?)$', line
            )
            if match:
                indent, key, path, filename, comma = match.groups()
                line = indent + f'"{key}": "{{{{.*{filename}}}}}"' + comma

            # Use CHECK for the first line, CHECK-NEXT for subsequent lines
            if i == 0:
                lines.append(f"// CHECK: {line}")
            else:
                lines.append(f"// CHECK-NEXT: {line}")
        return lines

    def format_tree_for_check(self, tree_str: str) -> List[str]:
        """Format tree dump output as CHECK comments."""
        lines = []
        tree_lines = [
            self._normalize_diagnostic_line(line)
            for line in tree_str.strip().split("\n")
            if line.strip()
        ]
        for i, line in enumerate(tree_lines):
            if i == 0:
                lines.append(f"// CHECK: {line}")
            else:
                lines.append(f"// CHECK-NEXT: {line}")
        return lines

    def _strip_ansi(self, text: str) -> str:
        return re.sub(r"\x1b\[[0-9;]*m", "", text)

    def _normalize_diagnostic_line(self, line: str) -> str:
        line = self._strip_ansi(line).rstrip()
        line = re.sub(
            r"/(?:[A-Za-z0-9_.-]+/)+(?P<file>[^/\s:]+\.zom)",
            lambda m: f"{{{{.*{m.group('file')}}}}}",
            line,
        )
        line = re.sub(
            r"(?:[A-Za-z]:\\\\)?(?:[A-Za-z0-9_.-]+\\\\)+(?P<file>[^\\\\\s:]+\.zom)",
            lambda m: f"{{{{.*{m.group('file')}}}}}",
            line,
        )
        line = re.sub(r"(?:/?[A-Za-z0-9_.-]+/)+zomc\b", "{{.*zomc}}", line)
        line = re.sub(
            r"(?:[A-Za-z]:\\\\)?(?:[A-Za-z0-9_.-]+\\\\)+zomc\b",
            "{{.*zomc}}",
            line,
        )
        return line

    def _extract_stable_diagnostic_lines(self, output: str) -> List[str]:
        output = self._strip_ansi(output)
        lines = []
        for line in output.splitlines():
            stripped = line.rstrip()
            if not stripped:
                continue
            if (
                stripped.startswith("Error [")
                or stripped.startswith("Warning [")
                or stripped.startswith("Note [")
            ):
                lines.append(stripped)
                continue

            compilation_failed_match = re.search(
                r"(Compilation failed[^.]*\.)", stripped
            )
            if compilation_failed_match:
                lines.append(compilation_failed_match.group(1))

        if lines:
            return lines

        fallback = []
        for line in output.splitlines():
            stripped = line.strip()
            if not stripped:
                continue
            fallback.append(stripped)
            if len(fallback) >= 10:
                break
        return fallback

    def format_diagnostics_for_check(self, output: str) -> List[str]:
        check_lines = []
        for line in output.splitlines():
            normalized = self._normalize_diagnostic_line(line)
            if not normalized.strip():
                continue
            if "zomc}} compile: Compilation failed" in normalized:
                continue
            if "zomc compile: Compilation failed" in normalized:
                continue
            if normalized.startswith("Try '") and "zomc}} compile --help" in normalized:
                continue
            if normalized.startswith("Try '") and "zomc compile --help" in normalized:
                continue
            if "{{.*" in normalized:
                check_lines.extend(self._format_path_sensitive_diagnostic_line(normalized))
            else:
                check_lines.append(f"// CHECK-LITERAL: {normalized}")
        if check_lines:
            return check_lines

        stable_lines = self._extract_stable_diagnostic_lines(output)
        return [
            f"// CHECK-LITERAL: {self._normalize_diagnostic_line(line)}"
            for line in stable_lines
        ]

    def _format_path_sensitive_diagnostic_line(self, line: str) -> List[str]:
        path_match = re.search(r"\{\{\.\*(?P<file>[^}]+\.zom)\}\}(?P<suffix>.*)$", line)
        if not path_match:
            return [f"// CHECK: {line}"]

        prefix = line[: path_match.start()]
        file = path_match.group("file")
        suffix = path_match.group("suffix")
        path_prefix_regex = r"(?:.*[/\\])?"
        path_regex = f"{path_prefix_regex}{re.escape(file)}{re.escape(suffix)}$"
        if prefix:
            return [f"// CHECK: {prefix} {{{{{path_regex}}}}}"]
        return [f"// CHECK: {{{{{path_regex}}}}}"]

    def _is_json_output(self, output: str) -> bool:
        candidate = output.strip()
        if not candidate:
            return False
        if not (candidate.startswith("{") or candidate.startswith("[")):
            return False
        try:
            json.loads(candidate)
            return True
        except Exception:
            return False

    def resolve_source_and_expectation(self, target: str) -> Tuple[Path, Path, str]:
        """Resolve a corpus source or AST expectation to both paths."""
        path = Path(target).resolve()

        if path.suffix == ".check":
            if (
                path == self.ast_expectation_root
                or self.ast_expectation_root in path.parents
            ):
                rel = path.relative_to(self.ast_expectation_root).with_suffix(".zom")
                return self.corpus_root / rel, path, "ast"
            if (
                path == self.diagnostics_expectation_root
                or self.diagnostics_expectation_root in path.parents
            ):
                rel = path.relative_to(self.diagnostics_expectation_root).with_suffix(
                    ".zom"
                )
                return self.corpus_root / rel, path, "diagnostics"
            raise RuntimeError(f"unsupported snapshot expectation path: {path}")

        if path.suffix == ".zom":
            rel = path.relative_to(self.corpus_root)
            return path, (self.ast_expectation_root / rel).with_suffix(".check"), "ast"

        raise RuntimeError(f"unsupported snapshot target suffix: {path}")

    def default_run_line(
        self, source_file: Path, returncode: int, snapshot_kind: str
    ) -> str:
        """Create a RUN line for a new expectation file."""
        rel = source_file.relative_to(self.corpus_root).as_posix()
        if snapshot_kind == "diagnostics":
            if returncode == 0:
                return f"// RUN: cd %corpus && %zomc compile --check {rel} > %t 2>&1"
            return f"// RUN: cd %corpus && ! %zomc compile --check {rel} > %t 2>&1"
        if returncode == 0:
            return f"// RUN: %zomc compile --dump-ast %corpus/{rel} | %FileCheck %s"
        return f"// RUN: ! %zomc compile --dump-ast %corpus/{rel} 2>&1 | %FileCheck %s"

    def default_filecheck_run_line(self, snapshot_kind: str) -> Optional[str]:
        """Create the secondary FileCheck RUN line for two-step tests."""
        if snapshot_kind == "diagnostics":
            return "// RUN: %FileCheck %s --input-file %t"
        return None

    def normalize_run_line(
        self, line: str, source_file: Path, returncode: int, snapshot_kind: str
    ) -> str:
        """Normalize RUN lines to the command whose output was regenerated."""
        expected = self.default_run_line(source_file, returncode, snapshot_kind)
        return line if line.strip() == expected else expected + "\n"

    def read_lines(self, path: Path) -> List[str]:
        """Read a text file and return lines."""
        with open(path, "r", encoding="utf-8") as f:
            return f.readlines()

    def should_skip_regeneration(self, path: Path) -> bool:
        """Return true for hand-written FileCheck tests that must not be regenerated."""
        if not path.exists():
            return False
        return any(
            line.strip().startswith(self.REGEN_SKIP_PREFIX)
            for line in self.read_lines(path)
        )

    def write_lines(self, path: Path, lines: List[str]):
        """Write lines to a text file."""
        path.parent.mkdir(parents=True, exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            f.writelines(lines)

    def remove_existing_checks(self, lines: List[str]) -> List[str]:
        """Remove existing CHECK comments from test file."""
        filtered_lines = []
        for line in lines:
            stripped = line.strip()
            if not (
                stripped.startswith("// CHECK")
                or stripped.startswith("// CHECK-NEXT")
                or stripped.startswith("// CHECK-NOT")
            ):
                filtered_lines.append(line)
        return filtered_lines

    def regenerate_snapshot(self, target: str, append_mode: bool = False):
        """Regenerate snapshot for a single test file."""
        source_file, expectation_file, snapshot_kind = self.resolve_source_and_expectation(
            target
        )
        if self.should_skip_regeneration(expectation_file):
            print(f"Skipping hand-written snapshot: {expectation_file}")
            return

        print(f"Regenerating snapshot for: {source_file}")

        returncode, output = self.run_zomc(str(source_file), snapshot_kind)
        output = output.rstrip()

        if snapshot_kind == "diagnostics":
            check_lines = self.format_diagnostics_for_check(output)
        elif returncode == 0:
            if self._is_json_output(output):
                check_lines = self.format_json_for_check(output)
            else:
                check_lines = self.format_tree_for_check(output)
        else:
            check_lines = self.format_diagnostics_for_check(output)

        if expectation_file.exists():
            lines = self.read_lines(expectation_file)
        else:
            lines = [self.default_run_line(source_file, returncode, snapshot_kind) + "\n"]

        if append_mode:
            # Remove trailing empty lines and ensure exactly 2 empty lines before CHECK comments
            while lines and lines[-1].strip() == "":
                lines.pop()

            # Ensure the last line ends with newline
            if lines and not lines[-1].endswith("\n"):
                lines[-1] = lines[-1] + "\n"

            # Add exactly 2 empty lines before CHECK comments
            lines.append("\n")
            lines.append("\n")

            for check_line in check_lines:
                lines.append(check_line + "\n")
        else:
            # Remove existing CHECK comments and trailing empty lines
            filtered_lines = self.remove_existing_checks(lines)

            if snapshot_kind == "diagnostics":
                filtered_lines = [
                    line
                    for line in filtered_lines
                    if not line.strip().startswith("// RUN:")
                ]
                filtered_lines.insert(
                    0,
                    self.default_run_line(source_file, returncode, snapshot_kind)
                    + "\n",
                )
                filecheck_run_line = self.default_filecheck_run_line(snapshot_kind)
                if filecheck_run_line is not None:
                    filtered_lines.insert(1, filecheck_run_line + "\n")
            else:
                has_run_line = any(
                    line.strip().startswith("// RUN:") for line in filtered_lines
                )
                if not has_run_line:
                    filtered_lines.insert(
                        0,
                        self.default_run_line(source_file, returncode, snapshot_kind)
                        + "\n",
                    )
                else:
                    filtered_lines = [
                        self.normalize_run_line(
                            line, source_file, returncode, snapshot_kind
                        )
                        if line.strip().startswith("// RUN:")
                        else line
                        for line in filtered_lines
                    ]

            # Remove trailing empty lines
            while filtered_lines and filtered_lines[-1].strip() == "":
                filtered_lines.pop()

            # Ensure the last line ends with newline
            if filtered_lines and not filtered_lines[-1].endswith("\n"):
                filtered_lines[-1] = filtered_lines[-1] + "\n"

            # Add exactly 2 empty lines before CHECK comments
            filtered_lines.append("\n")
            filtered_lines.append("\n")

            for check_line in check_lines:
                filtered_lines.append(check_line + "\n")

            lines = filtered_lines

        # Write back to file
        self.write_lines(expectation_file, lines)
        print(f"✓ Updated {expectation_file}")

    def regenerate_directory(self, directory: str, append_mode: bool = False):
        """Regenerate snapshots for all corpus sources or expectations."""
        directory_path = Path(directory).resolve()
        suffix = (
            ".check"
            if directory_path == self.ast_expectation_root
            or self.ast_expectation_root in directory_path.parents
            or directory_path == self.diagnostics_expectation_root
            or self.diagnostics_expectation_root in directory_path.parents
            else ".zom"
        )
        test_files = []
        for root, dirs, files in os.walk(directory):
            for file in files:
                if file.endswith(suffix):
                    test_files.append(os.path.join(root, file))

        if not test_files:
            print(f"No {suffix} test files found in {directory}")
            return

        for test_file in sorted(test_files):
            try:
                self.regenerate_snapshot(test_file, append_mode)
            except Exception as e:
                print(f"✗ Failed to update {test_file}: {e}")


def main():
    parser = argparse.ArgumentParser(
        description="Regenerate snapshots for ZomLang conformance tests"
    )
    parser.add_argument(
        "target", help="Test file or directory to regenerate snapshots for"
    )
    parser.add_argument(
        "--zomc-path", help="Path to zomc compiler (auto-detected if not specified)"
    )
    parser.add_argument(
        "--cmake-binary-dir",
        help="CMake binary dir (defaults to $CMAKE_BINARY_DIR if set)",
    )
    parser.add_argument(
        "--preset",
        help="CMake preset name (uses build-<preset> when auto-detecting)",
    )
    parser.add_argument(
        "--append",
        action="store_true",
        help="Append CHECK comments instead of replacing existing ones",
    )

    args = parser.parse_args()

    if not os.path.exists(args.target):
        print(f"Error: {args.target} does not exist")
        sys.exit(1)

    regenerator = SnapshotRegenerator(
        args.zomc_path, args.cmake_binary_dir, args.preset
    )

    try:
        if os.path.isfile(args.target):
            regenerator.regenerate_snapshot(args.target, args.append)
        elif os.path.isdir(args.target):
            regenerator.regenerate_directory(args.target, args.append)
        else:
            print(f"Error: {args.target} is neither a file nor a directory")
            sys.exit(1)

        print("\n✓ Snapshot regeneration completed successfully!")

    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
