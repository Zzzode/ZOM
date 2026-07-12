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
        self,
        check_file: str,
        input_file: str,
        strict_whitespace: bool = False,
        check_prefix: str = "CHECK",
    ):
        self.check_file = check_file
        self.input_file = input_file
        self.strict_whitespace = strict_whitespace
        self.check_prefix = check_prefix
        self.check_lines = []
        self.input_content = ""

    def load_files(self):
        """Load check patterns and input content."""
        try:
            with open(self.check_file, "r", encoding="utf-8") as f:
                content = f.read()
                # Extract CHECK lines
                for raw_line in content.splitlines():
                    line = raw_line.strip()
                    directive = raw_line.lstrip()
                    literal = f"// {self.check_prefix}-LITERAL:"
                    next_literal = f"// {self.check_prefix}-NEXT-LITERAL:"
                    same = f"// {self.check_prefix}-SAME:"
                    next_check = f"// {self.check_prefix}-NEXT:"
                    not_check = f"// {self.check_prefix}-NOT:"
                    check = f"// {self.check_prefix}:"
                    if directive.startswith(literal):
                        pattern = directive[len(literal) :]
                        if pattern.startswith(" "):
                            pattern = pattern[1:]
                        self.check_lines.append(("CHECK-LITERAL", pattern))
                    elif directive.startswith(next_literal):
                        pattern = directive[len(next_literal) :]
                        if pattern.startswith(" "):
                            pattern = pattern[1:]
                        self.check_lines.append(("CHECK-NEXT-LITERAL", pattern))
                    elif line.startswith(check):
                        pattern = line[len(check) :].strip()
                        self.check_lines.append(("CHECK", pattern))
                    elif directive.startswith(same):
                        pattern = directive[len(same) :]
                        if pattern.startswith(" "):
                            pattern = pattern[1:]
                        self.check_lines.append(("CHECK-SAME", pattern))
                    elif line.startswith(next_check):
                        pattern = line[len(next_check) :].strip()
                        self.check_lines.append(("CHECK-NEXT", pattern))
                    elif line.startswith(not_check):
                        pattern = line[len(not_check) :].strip()
                        self.check_lines.append(("CHECK-NOT", pattern))

            with open(self.input_file, "r", encoding="utf-8") as f:
                self.input_content = f.read()

        except FileNotFoundError as e:
            raise FileCheckError(f"File not found: {e}")
        except Exception as e:
            raise FileCheckError(f"Error reading files: {e}")

    def normalize_json(self, content: str) -> str:
        """Normalize JSON content for comparison."""
        stripped = re.sub(r"\x1b\[[0-9;]*m", "", content)
        return "\n".join(line.rstrip() for line in stripped.splitlines())

    def build_regex_pattern(self, pattern: str) -> Optional[str]:
        """Convert a FileCheck-style pattern with inline regex blocks into a regex."""
        if "{{" not in pattern and "[[" not in pattern:
            return None

        regex_parts = []
        cursor = 0

        while cursor < len(pattern):
            regex_start = pattern.find("{{", cursor)
            variable_start = pattern.find("[[", cursor)

            starts = [pos for pos in (regex_start, variable_start) if pos != -1]
            if not starts:
                regex_parts.append(re.escape(pattern[cursor:]))
                break

            next_start = min(starts)
            if next_start > cursor:
                regex_parts.append(re.escape(pattern[cursor:next_start]))

            if next_start == regex_start:
                regex_end = pattern.find("}}", regex_start + 2)
                if regex_end == -1:
                    regex_parts.append(re.escape(pattern[regex_start:]))
                    break

                regex_parts.append(pattern[regex_start + 2 : regex_end])
                cursor = regex_end + 2
                continue

            variable_end = pattern.find("]]", variable_start + 2)
            if variable_end == -1:
                regex_parts.append(re.escape(pattern[variable_start:]))
                break

            variable_body = pattern[variable_start + 2 : variable_end]
            if ":" in variable_body:
                _, variable_regex = variable_body.split(":", 1)
                regex_parts.append(f"(?:{variable_regex})")
            else:
                regex_parts.append(re.escape(variable_body))

            cursor = variable_end + 2

        return "".join(regex_parts)

    def match_pattern(self, pattern: str, content: str) -> bool:
        """Check if pattern matches in content."""
        regex_pattern = self.build_regex_pattern(pattern)
        if regex_pattern is not None:
            return bool(re.search(regex_pattern, content, re.MULTILINE))

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
        last_match_line = None

        for check_type, pattern in self.check_lines:
            if check_type == "CHECK":
                # Find pattern starting from current line
                found = False
                for i in range(current_line, len(input_lines)):
                    if self.match_pattern(pattern, input_lines[i]):
                        current_line = i + 1
                        last_match_line = i
                        found = True
                        break

                if not found:
                    # Try matching against the entire remaining content
                    remaining_content = "\n".join(input_lines[current_line:])
                    if not self.match_pattern(pattern, remaining_content):
                        raise FileCheckError(f"CHECK pattern not found: {pattern}")

            elif check_type == "CHECK-LITERAL":
                found = False
                for i in range(current_line, len(input_lines)):
                    if input_lines[i] == pattern:
                        current_line = i + 1
                        last_match_line = i
                        found = True
                        break

                if not found:
                    raise FileCheckError(f"CHECK-LITERAL line not found: {pattern}")

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

                last_match_line = current_line
                current_line += 1

            elif check_type == "CHECK-SAME":
                if last_match_line is None:
                    raise FileCheckError(
                        f"CHECK-SAME has no previous matched line: {pattern}"
                    )

                if not self.match_pattern(pattern, input_lines[last_match_line]):
                    raise FileCheckError(
                        f"CHECK-SAME pattern not found on line {last_match_line + 1}: {pattern}"
                    )

            elif check_type == "CHECK-NEXT-LITERAL":
                if current_line >= len(input_lines):
                    raise FileCheckError(
                        f"CHECK-NEXT-LITERAL pattern beyond end of file: {pattern}"
                    )

                if input_lines[current_line] != pattern:
                    raise FileCheckError(
                        f"CHECK-NEXT-LITERAL line not found on line {current_line + 1}: {pattern}"
                    )

                last_match_line = current_line
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

    checker = None
    try:
        checker = FileChecker(
            args.check_file, input_file, args.strict_whitespace, args.check_prefix
        )
        if checker.check():
            print(f"FileCheck passed: {args.check_file}")
            sys.exit(0)
        else:
            print(f"FileCheck failed: {args.check_file}")
            sys.exit(1)

    except FileCheckError as e:
        print(f"FileCheck error: {e}", file=sys.stderr)
        if checker is not None and checker.input_content:
            normalized = checker.normalize_json(checker.input_content)
            excerpt = normalized[:4096]
            print("FileCheck input:", file=sys.stderr)
            print(excerpt, file=sys.stderr)
            if len(normalized) > len(excerpt):
                print("[input truncated]", file=sys.stderr)
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
