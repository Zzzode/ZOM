#!/usr/bin/env python3
"""Reject version generations in unreleased internal ZOM contracts."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SELF = Path("scripts/check-no-internal-versioning.py")

POLICY_DEFINITION_FILES = {
    Path("AGENTS.md"),
    Path(".codex/rules/design-principles.md"),
    Path("docs/rfc/README.md"),
}

EXCLUDED_DIRECTORY_NAMES = {
    ".git",
    ".venv",
    "node_modules",
    "thirdparty",
    "vendor",
}

DOMAIN_VERSION = re.compile(
    r"(?<![A-Za-z0-9_.-])zom(?:\.[a-z0-9][a-z0-9_-]*)+\.v[0-9]+"
    r"(?![A-Za-z0-9_.-])"
)
API_GENERATION = re.compile(
    r"\b(?:[A-Z][A-Za-z0-9_]*V[0-9]+|V[0-9]+[A-Z][A-Za-z0-9_]*)\b"
)
CONTRACT_VERSION_IDENTIFIER = re.compile(
    r"\b[A-Za-z_][A-Za-z0-9_]*"
    r"(?:SchemaVersion|FormatVersion|ProtocolVersion|WireVersion)"
    r"[A-Za-z0-9_]*\b"
)
NUMERIC_VERSION_FIELD = re.compile(
    r"""(?x)
    ["']?
    (?:schema_version|schemaVersion|format_version|formatVersion|
       protocol_version|protocolVersion|wire_version|wireVersion)
    ["']?
    \s*[:=]\s*[0-9]+\b
    """
)
GENERIC_NUMERIC_VERSION_FIELD = re.compile(
    r"""(?x)
    ^\s*(?:"version"|version)
    \s*[:=]\s*[0-9]+\b
    """
)
ZOM_SPEC_GENERATION = re.compile(
    r"(?ix)"
    r"(?:\bZOM[\s_-]+v[0-9]+\b|"
    r"\b(?:ZOM\s+)?(?:language|spec(?:ification)?|syntax|grammar)"
    r"\s+(?:generation\s+)?v[0-9]+\b)"
)
ZOM_OWNED_STRING_GENERATION = re.compile(
    r"(?i)(?:\bzom[-_.]v[0-9]+\b|\bzom-lock-[0-9]+\b)"
)
INTERNAL_VERSIONED_TOKEN = re.compile(
    r"(?i)(?<![/@=A-Za-z0-9_.-])v[0-9]+\b(?!\.[0-9])"
)
INTERNAL_STRING_GENERATION = re.compile(
    r"(?i)\b(?:tar-zstd|[a-z0-9-]*(?:schema|format|protocol|wire|"
    r"oracles?|scenarios))[-_.]v[0-9]+\b"
)
ENCODED_INTERNAL_GENERATION = re.compile(
    r"""(?ix)
    ['"][-_.]['"]\s*,\s*
    ['"]v['"]\s*,\s*
    ['"][0-9]+['"]
    """
)
VERSIONED_FILE_COMPONENT = re.compile(r"(?i)(?:^|[-_.])v[0-9]+(?=[-_.]|$)")

EXTERNAL_IDENTIFIER_PREFIXES = (
    "Antlr",
    "Arm",
    "Avx",
    "Cxx",
    "Http",
    "IPv",
    "Sha",
    "Tls",
    "Unicode",
    "Utf",
    "X86",
)

EXTERNAL_VERSION_CONTEXTS = (
    re.compile(r"\bcgroup\s+v2\b", re.IGNORECASE),
    re.compile(r"\bUnicode(?:\s+(?:Standard|License))?\s+v[0-9]+\b", re.IGNORECASE),
    re.compile(
        r"\b(?:ANTLR|LLVM|Clang|CMake|Python|Rust|Swift|Go|HTTP|TLS)"
        r"\s+v[0-9]+\b",
        re.IGNORECASE,
    ),
    re.compile(r"\bIP address\s*\(v4\s+or\s+v6\)", re.IGNORECASE),
    re.compile(r"\bv[46]-only\b", re.IGNORECASE),
    re.compile(r"\bV8\b"),
)

PATH_CATEGORY_ALLOWLIST = {
    (Path("CMakePresets.json"), "numeric-generic-version-field"),
}

PATH_PREFIX_CATEGORY_ALLOWLIST = (
    (Path("libraries/zc/async"), "internal-contract-generation"),
    (Path("libraries/zc/core/cidr.cc"), "internal-contract-generation"),
    (Path("libraries/zc/unittests/tls"), "internal-contract-generation"),
)


@dataclass(frozen=True)
class Finding:
    path: Path
    line: int
    column: int
    category: str
    text: str


def is_excluded_path(path: Path) -> bool:
    if path == SELF or path in POLICY_DEFINITION_FILES:
        return True
    if path == Path("docs/reports") or Path("docs/reports") in path.parents:
        return True
    if any(part in EXCLUDED_DIRECTORY_NAMES for part in path.parts):
        return True
    return bool(path.parts and path.parts[0].startswith("build"))


def repository_files(root: Path) -> list[Path]:
    completed = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"],
        cwd=root,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if completed.returncode == 0:
        paths = [
            Path(item.decode("utf-8"))
            for item in completed.stdout.split(b"\0")
            if item
        ]
    else:
        paths = [
            path.relative_to(root)
            for path in root.rglob("*")
            if path.is_file()
        ]
    return sorted(path for path in paths if not is_excluded_path(path))


def read_text(path: Path) -> str | None:
    try:
        data = path.read_bytes()
    except OSError:
        return None
    if b"\0" in data:
        return None
    try:
        return data.decode("utf-8")
    except UnicodeDecodeError:
        return None


def allowed_api_identifier(identifier: str) -> bool:
    return identifier in {"IPPROTO_IPV6", "RSV1", "V6MAPPED"} or identifier.startswith(
        EXTERNAL_IDENTIFIER_PREFIXES
    )


def path_category_allowed(path: Path, category: str) -> bool:
    if (path, category) in PATH_CATEGORY_ALLOWLIST:
        return True
    return any(
        category == allowed_category
        and (path == prefix or prefix in path.parents)
        for prefix, allowed_category in PATH_PREFIX_CATEGORY_ALLOWLIST
    )


def externally_allowed_span(line: str, start: int, end: int) -> bool:
    return any(
        context.start() <= start and end <= context.end()
        for pattern in EXTERNAL_VERSION_CONTEXTS
        for context in pattern.finditer(line)
    )


def scan_text(path: Path, text: str) -> list[Finding]:
    findings: list[Finding] = []
    checks = (
        ("internal-domain-generation", DOMAIN_VERSION),
        ("product-api-generation", API_GENERATION),
        ("internal-contract-version-identifier", CONTRACT_VERSION_IDENTIFIER),
        ("numeric-internal-version-field", NUMERIC_VERSION_FIELD),
        ("numeric-generic-version-field", GENERIC_NUMERIC_VERSION_FIELD),
        ("zom-spec-generation", ZOM_SPEC_GENERATION),
        ("zom-owned-string-generation", ZOM_OWNED_STRING_GENERATION),
        ("internal-string-generation", INTERNAL_STRING_GENERATION),
        ("encoded-internal-generation", ENCODED_INTERNAL_GENERATION),
        ("internal-contract-generation", INTERNAL_VERSIONED_TOKEN),
    )
    for line_number, line in enumerate(text.splitlines(), start=1):
        for category, pattern in checks:
            for match in pattern.finditer(line):
                matched = match.group(0)
                if path_category_allowed(path, category):
                    continue
                if category == "product-api-generation" and allowed_api_identifier(matched):
                    continue
                if externally_allowed_span(line, match.start(), match.end()):
                    continue
                findings.append(
                    Finding(
                        path=path,
                        line=line_number,
                        column=match.start() + 1,
                        category=category,
                        text=matched,
                    )
                )
    for part in path.parts:
        match = VERSIONED_FILE_COMPONENT.search(part)
        if match:
            findings.append(
                Finding(
                    path=path,
                    line=0,
                    column=match.start() + 1,
                    category="versioned-internal-file-name",
                    text=part,
                )
            )
    return findings


def scan_repository(root: Path) -> list[Finding]:
    findings: list[Finding] = []
    for relative in repository_files(root):
        text = read_text(root / relative)
        if text is not None:
            findings.extend(scan_text(relative, text))
    return sorted(
        findings,
        key=lambda finding: (
            str(finding.path),
            finding.line,
            finding.column,
            finding.category,
        ),
    )


def print_findings(findings: list[Finding]) -> None:
    for finding in findings:
        location = (
            f"{finding.path}:{finding.line}:{finding.column}"
            if finding.line
            else f"{finding.path}:filename"
        )
        print(
            f"{location}: {finding.category}: {finding.text}",
            file=sys.stderr,
        )


def run_check(root: Path) -> int:
    findings = scan_repository(root)
    if findings:
        print_findings(findings)
        print(
            f"internal versioning check failed with {len(findings)} finding(s)",
            file=sys.stderr,
        )
        return 1
    print("internal versioning check passed")
    return 0


def run_self_test() -> int:
    cases = {
        "domain.cc": (
            'constexpr auto domain = "zom.query.example.v1";\n',
            "internal-domain-generation",
        ),
        "api.h": ("class QuerySnapshotV1 final {};\n", "product-api-generation"),
        "spec.md": ("ZOM v2 syntax is current.\n", "zom-spec-generation"),
        "contract.h": (
            "uint32_t keyWireVersion() const;\n",
            "internal-contract-version-identifier",
        ),
        "schema.json": ('{"schema_version": 1}\n', "numeric-internal-version-field"),
        "manifest.json": ('{\n  "version": 1\n}\n', "numeric-generic-version-field"),
        "owned-string.cc": (
            'constexpr auto abi = "zom-v1";\n',
            "zom-owned-string-generation",
        ),
        "archive.txt": ("tar-zstd-v1\n", "internal-string-generation"),
        "encoded-domain.cc": (
            "const char domain[] = {'z', 'o', 'm', '.', 'v', '1'};\n",
            "encoded-internal-generation",
        ),
        "contract.md": (
            "module-interface v1 replaces the v0 decoder.\n",
            "internal-contract-generation",
        ),
    }
    observed: set[str] = set()
    with tempfile.TemporaryDirectory(prefix="zom-no-internal-versioning-") as directory:
        root = Path(directory)
        for name, (content, expected) in cases.items():
            path = Path(name)
            (root / path).write_text(content, encoding="utf-8")
            findings = scan_text(path, content)
            if not any(finding.category == expected for finding in findings):
                print(f"self-test failed to reject {name} as {expected}", file=sys.stderr)
                return 1
            observed.add(expected)
        versioned_fixture = Path("fixtures/package-oracles-v1.json")
        file_findings = scan_text(versioned_fixture, "{}\n")
        if not any(
            finding.category == "versioned-internal-file-name"
            for finding in file_findings
        ):
            print("self-test failed to reject a versioned fixture name", file=sys.stderr)
            return 1

    required = {
        "internal-domain-generation",
        "product-api-generation",
        "zom-spec-generation",
    }
    if not required.issubset(observed):
        print("self-test did not exercise all required regression classes", file=sys.stderr)
        return 1

    allowed = (
        "License, Version 2.0; Unicode License v3; Unicode data 15.1; C++23; "
        "ANTLR v4; org.antlr.v4; LLVM v18; OpenSSL 3.0; cgroup v2; cgroup-v2; "
        "IPv4 and IPv6; IP address (v4 or v6); OpenBSD v4-only; V8; RFC 0017; "
        "git status --porcelain=v1; actions/checkout@v4; stateDiagram-v2; "
        "/api/v1; package v1.2.3; zom-audit-2026; ZOM-G4-PATTERN-001; ZOM_TARGET_1\n"
    )
    if scan_text(Path("external-standards.txt"), allowed):
        print("self-test rejected an explicitly allowed external version", file=sys.stderr)
        return 1

    print("internal versioning self-test passed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Reject internal ZOM contract generations and versioned fixtures"
    )
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()
    if arguments.self_test:
        return run_self_test()
    return run_check(ROOT)


if __name__ == "__main__":
    raise SystemExit(main())
