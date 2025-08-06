#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Snapshot regeneration tool for ZomLang AST tests.

This tool automatically regenerates CHECK comments in lit test files
by running the zomc compiler and capturing its AST output.
"""

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import List, Optional


class SnapshotRegenerator:
    """Tool to regenerate test snapshots for ZomLang AST tests."""

    def __init__(self, zomc_path: str):
        self.zomc_path = zomc_path

    def find_zomc(self) -> Optional[str]:
        """Find zomc compiler in build directories."""
        if self.zomc_path and os.path.exists(self.zomc_path):
            return self.zomc_path

        # Try to find zomc in build directory
        # Go up from tests/tools to project root
        script_dir = Path(__file__).parent.parent.parent.parent.parent
        build_dirs = ["build-sanitizer", "build", "build-debug", "build-release"]

        for build_dir in build_dirs:
            potential_path = (
                script_dir
                / build_dir
                / "products"
                / "zomlang"
                / "utils"
                / "zomc"
                / "zomc"
            )
            if potential_path.exists():
                return str(potential_path)

        # Also try current working directory
        cwd = Path.cwd()
        for build_dir in build_dirs:
            potential_path = (
                cwd / build_dir / "products" / "zomlang" / "utils" / "zomc" / "zomc"
            )
            if potential_path.exists():
                return str(potential_path)

        return None

    def run_zomc(self, test_file: str) -> str:
        """Run zomc compiler and get AST output."""
        zomc = self.find_zomc()
        if not zomc:
            raise RuntimeError("Could not find zomc compiler")

        try:
            result = subprocess.run(
                [zomc, "compile", "--dump-ast", test_file],
                capture_output=True,
                text=True,
                check=True,
            )
            return result.stdout.strip()
        except subprocess.CalledProcessError as e:
            raise RuntimeError(f"zomc failed: {e.stderr}")

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

    def read_test_file(self, test_file: str) -> List[str]:
        """Read test file and return lines."""
        with open(test_file, "r", encoding="utf-8") as f:
            return f.readlines()

    def write_test_file(self, test_file: str, lines: List[str]):
        """Write lines back to test file."""
        with open(test_file, "w", encoding="utf-8") as f:
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

    def regenerate_snapshot(self, test_file: str, append_mode: bool = False):
        """Regenerate snapshot for a single test file."""
        print(f"Regenerating snapshot for: {test_file}")

        # Get AST output from zomc
        ast_output = self.run_zomc(test_file)

        # Format as CHECK comments
        check_lines = self.format_json_for_check(ast_output)

        # Read existing test file
        lines = self.read_test_file(test_file)

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
        self.write_test_file(test_file, lines)
        print(f"✓ Updated {test_file}")

    def regenerate_directory(self, directory: str, append_mode: bool = False):
        """Regenerate snapshots for all .zom files in directory."""
        test_files = []
        for root, dirs, files in os.walk(directory):
            for file in files:
                if file.endswith(".zom"):
                    test_files.append(os.path.join(root, file))

        if not test_files:
            print(f"No .zom test files found in {directory}")
            return

        for test_file in sorted(test_files):
            try:
                self.regenerate_snapshot(test_file, append_mode)
            except Exception as e:
                print(f"✗ Failed to update {test_file}: {e}")


def main():
    parser = argparse.ArgumentParser(
        description="Regenerate snapshots for ZomLang AST tests"
    )
    parser.add_argument(
        "target", help="Test file or directory to regenerate snapshots for"
    )
    parser.add_argument(
        "--zomc-path", help="Path to zomc compiler (auto-detected if not specified)"
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

    regenerator = SnapshotRegenerator(args.zomc_path)

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
