#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Simple FileCheck implementation for ZomLang AST tests.

This is a simplified version of LLVM's FileCheck tool, designed specifically
for checking AST dump outputs in JSON format.
"""

import argparse
import json
import re
import sys
from typing import List, Dict, Any, Optional


class FileCheckError(Exception):
    """Exception raised when FileCheck fails."""

    pass


class FileChecker:
    """Simple FileCheck implementation for AST testing."""

    def __init__(
        self, check_file: str, input_file: str, strict_whitespace: bool = False
    ):
        self.check_file = check_file
        self.input_file = input_file
        self.strict_whitespace = strict_whitespace
        self.check_lines = []
        self.input_content = ""

    def load_files(self):
        """Load check patterns and input content."""
        try:
            with open(self.check_file, "r", encoding="utf-8") as f:
                content = f.read()
                # Extract CHECK lines
                for line in content.splitlines():
                    line = line.strip()
                    if line.startswith("// CHECK:"):
                        pattern = line[9:].strip()  # Remove '// CHECK:' prefix
                        self.check_lines.append(("CHECK", pattern))
                    elif line.startswith("// CHECK-NEXT:"):
                        pattern = line[14:].strip()  # Remove '// CHECK-NEXT:' prefix
                        self.check_lines.append(("CHECK-NEXT", pattern))
                    elif line.startswith("// CHECK-NOT:"):
                        pattern = line[13:].strip()  # Remove '// CHECK-NOT:' prefix
                        self.check_lines.append(("CHECK-NOT", pattern))

            with open(self.input_file, "r", encoding="utf-8") as f:
                self.input_content = f.read()

        except FileNotFoundError as e:
            raise FileCheckError(f"File not found: {e}")
        except Exception as e:
            raise FileCheckError(f"Error reading files: {e}")

    def normalize_json(self, content: str) -> str:
        """Normalize JSON content for comparison."""
        # Return content as-is to match original zomc output format
        return content

    def match_pattern(self, pattern: str, content: str) -> bool:
        """Check if pattern matches in content."""
        # Handle special patterns
        if pattern.startswith("{{") and pattern.endswith("}}"):
            # Regex pattern
            regex_pattern = pattern[2:-2]
            return bool(re.search(regex_pattern, content, re.MULTILINE))

        # Handle patterns with quoted regex values like "{{.*filename}}"
        quoted_regex_match = re.search(r'"\{\{([^}]+)\}\}"', pattern)
        if quoted_regex_match:
            regex_part = quoted_regex_match.group(1)
            # Create a pattern to find the corresponding quoted value in content
            # Extract the context around the quoted regex
            before_quote = pattern[: quoted_regex_match.start()]
            after_quote = pattern[quoted_regex_match.end() :]

            # Escape special regex characters in the context
            before_escaped = re.escape(before_quote)
            after_escaped = re.escape(after_quote)

            # Create a regex to match the structure and capture the quoted value
            full_pattern = before_escaped + r'"([^"]+)"' + after_escaped

            # Search for the pattern in content
            match = re.search(full_pattern, content)
            if match:
                quoted_value = match.group(1)
                return bool(re.search(regex_part, quoted_value))
            return False

        # Handle variable substitution patterns like [[VAR:.*]]
        pattern = re.sub(r"\[\[\w+:([^\]]+)\]\]", r"\1", pattern)

        # Simple substring match
        if not self.strict_whitespace:
            # For non-strict whitespace, just do substring match
            return pattern.strip() in content
        else:
            return pattern in content

    def check(self) -> bool:
        """Perform the FileCheck operation."""
        self.load_files()

        if not self.check_lines:
            # No CHECK lines found, consider it a pass
            return True

        # Normalize input content if it's JSON
        normalized_input = self.normalize_json(self.input_content)
        input_lines = normalized_input.splitlines()

        current_line = 0

        for check_type, pattern in self.check_lines:
            if check_type == "CHECK":
                # Find pattern starting from current line
                found = False
                for i in range(current_line, len(input_lines)):
                    if self.match_pattern(pattern, input_lines[i]):
                        current_line = i + 1
                        found = True
                        break

                if not found:
                    # Try matching against the entire remaining content
                    remaining_content = "\n".join(input_lines[current_line:])
                    if not self.match_pattern(pattern, remaining_content):
                        raise FileCheckError(f"CHECK pattern not found: {pattern}")

            elif check_type == "CHECK-NEXT":
                # Pattern must match the very next line
                if current_line >= len(input_lines):
                    raise FileCheckError(
                        f"CHECK-NEXT pattern beyond end of file: {pattern}"
                    )

                if not self.match_pattern(pattern, input_lines[current_line]):
                    raise FileCheckError(
                        f"CHECK-NEXT pattern not found on line {current_line + 1}: {pattern}"
                    )

                current_line += 1

            elif check_type == "CHECK-NOT":
                # Pattern must NOT be found in remaining content
                remaining_content = "\n".join(input_lines[current_line:])
                if self.match_pattern(pattern, remaining_content):
                    raise FileCheckError(
                        f"CHECK-NOT pattern found (should not exist): {pattern}"
                    )

        return True


def main():
    parser = argparse.ArgumentParser(
        description="Simple FileCheck implementation for AST tests"
    )
    parser.add_argument("check_file", help="File containing CHECK patterns")
    parser.add_argument("--input-file", help="Input file to check against")
    parser.add_argument(
        "--strict-whitespace",
        action="store_true",
        help="Enable strict whitespace matching",
    )
    parser.add_argument(
        "--check-prefix", default="CHECK", help="Check prefix to use (default: CHECK)"
    )

    args = parser.parse_args()

    # If no input file specified, read from stdin
    input_file = args.input_file
    if not input_file:
        import tempfile

        with tempfile.NamedTemporaryFile(mode="w", delete=False, suffix=".tmp") as tmp:
            tmp.write(sys.stdin.read())
            input_file = tmp.name

    try:
        checker = FileChecker(args.check_file, input_file, args.strict_whitespace)
        if checker.check():
            print(f"FileCheck passed: {args.check_file}")
            sys.exit(0)
        else:
            print(f"FileCheck failed: {args.check_file}")
            sys.exit(1)

    except FileCheckError as e:
        print(f"FileCheck error: {e}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"Unexpected error: {e}", file=sys.stderr)
        sys.exit(1)
    finally:
        # Clean up temporary file if created
        if not args.input_file and input_file:
            import os

            try:
                os.unlink(input_file)
            except:
                pass


if __name__ == "__main__":
    main()
