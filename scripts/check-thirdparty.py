#!/usr/bin/env python3
# Copyright (c) 2026 Zode.Z. All rights reserved
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Verify the integrity of vendored general-purpose third-party libraries.

Each subdirectory of ``thirdparty`` that carries a ``third-party-manifest.json``
is checked: every file the manifest lists must exist on disk and hash to the
recorded SHA-256, no listed file may be missing, and the enabled sources/headers
and license must be among the listed files. The manifest records the upstream
release tarball's own SHA-256 for provenance; this script verifies the extracted
files that actually live in the tree, so a silent edit to a vendored source is a
hard failure.

This is deliberately separate from ``tests/tools/check-vendored-dependencies.py``,
which is scoped to the RFC 0012 package-manager vendor directory
(``compiler/driver/package/vendor``) and its five package-resolver dependencies.
General-purpose libraries linked by the compiler proper live under ``thirdparty``
and are governed by this script instead. Only directories with a manifest are
verified, so the non-built ``thirdparty/llvm`` and ``thirdparty/unicode`` lock and
reference assets are ignored.
"""

from __future__ import annotations

import hashlib
import json
import sys
from pathlib import Path

THIRD_PARTY_ROOT = Path(__file__).resolve().parents[1] / "thirdparty"
MANIFEST_NAME = "third-party-manifest.json"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    digest.update(path.read_bytes())
    return digest.hexdigest()


def verify_library(manifest_path: Path) -> list[str]:
    errors: list[str] = []
    root = manifest_path.parent
    rel_root = root.relative_to(THIRD_PARTY_ROOT.parents[1])
    try:
        manifest = json.loads(manifest_path.read_text())
    except json.JSONDecodeError as exc:
        return [f"{rel_root}/{MANIFEST_NAME}: invalid JSON: {exc}"]

    listed = {entry["path"]: entry["sha256"] for entry in manifest.get("files", [])}
    if not listed:
        errors.append(f"{rel_root}: manifest lists no files")

    # Every listed file must exist and match its digest.
    for rel_path, expected in listed.items():
        target = root / rel_path
        if not target.is_file():
            errors.append(f"{rel_root}/{rel_path}: listed in manifest but missing on disk")
            continue
        actual = sha256_file(target)
        if actual != expected:
            errors.append(
                f"{rel_root}/{rel_path}: sha256 mismatch "
                f"(manifest {expected[:12]}..., disk {actual[:12]}...)"
            )

    # Enabled sources/headers and the license must be tracked files.
    for key in ("enabled_sources", "enabled_headers"):
        for rel_path in manifest.get(key, []):
            if rel_path not in listed:
                errors.append(f"{rel_root}: {key} entry {rel_path} is not a tracked file")
    license_path = manifest.get("license")
    if license_path and license_path not in listed:
        errors.append(f"{rel_root}: license {license_path} is not a tracked file")

    # The upstream archive SHA-256 must be a full hex digest for provenance.
    archive_sha = manifest.get("archive_sha256", "")
    if len(archive_sha) != 64 or any(c not in "0123456789abcdef" for c in archive_sha):
        errors.append(f"{rel_root}: archive_sha256 is not a 64-char hex digest")

    return errors


def main() -> int:
    if not THIRD_PARTY_ROOT.is_dir():
        print(f"no third-party root at {THIRD_PARTY_ROOT}")
        return 0
    manifests = sorted(THIRD_PARTY_ROOT.glob(f"*/{MANIFEST_NAME}"))
    if not manifests:
        print("no third-party manifests to verify")
        return 0
    all_errors: list[str] = []
    for manifest_path in manifests:
        all_errors.extend(verify_library(manifest_path))
    if all_errors:
        print("third-party integrity check FAILED:")
        for error in all_errors:
            print(f"  - {error}")
        return 1
    print(f"third-party integrity check passed ({len(manifests)} library/libraries)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
