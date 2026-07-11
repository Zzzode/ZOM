#!/usr/bin/env python3

import argparse
import hashlib
import json
import stat
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[4]
VENDOR_ROOT = ROOT / "products" / "zomlang" / "compiler" / "driver" / "package" / "vendor"
MANIFEST_PATH = VENDOR_ROOT / "vendor-manifest.json"
CMAKE_PATH = VENDOR_ROOT / "CMakeLists.txt"
EMPTY_SHA256 = hashlib.sha256(b"").hexdigest()

ENABLED_SOURCE_VARIABLES = {
    "libsodium": ("ZOM_VENDOR_SODIUM_SOURCES", "ZOM_VENDOR_SODIUM_ROOT", "libsodium"),
    "zstd": ("ZOM_VENDOR_ZSTD_SOURCES", "ZOM_VENDOR_ZSTD_ROOT", ""),
}

DEPENDENCIES = {
    "libarchive": {
        "version": "v3.8.8",
        "commit": "7219b0134d771dc4b51bf86b4d01761b87398b1b",
        "archive_url": "https://github.com/libarchive/libarchive/archive/refs/tags/v3.8.8.tar.gz",
        "archive_sha256": "528f9c91e11238cbb5ce6d79b20fa3bb48a5cd124008036af1913d84fc5ba420",
        "spdx": "BSD-2-Clause",
        "license": "LICENSE",
        "compile_options": [
            "C11",
            "static",
            "posix-ustar-only",
            "external-zstd-stream",
            "no-filter-autodiscovery",
        ],
    },
    "libsodium": {
        "version": "1.0.22-RELEASE",
        "commit": "77e1ce5d6dee871c49ef211222ba18ef0c486bda",
        "archive_url": "https://github.com/jedisct1/libsodium/archive/refs/tags/1.0.22-RELEASE.tar.gz",
        "archive_sha256": "5838bb0c3da6148c24ebe531d1ed1297de9a87aea77d426bcd99f289e681631c",
        "spdx": "ISC",
        "license": "LICENSE",
        "compile_options": [
            "C11",
            "static",
            "minimal-link-closure",
            "no-assembly",
            "CONFIGURED=1",
            "MINIMAL=1",
            "SODIUM_LIBRARY_MINIMAL=1",
            "SODIUM_STATIC=1",
            "HAVE_POSIX_MEMALIGN=1:on-UNIX",
            "HAVE_PTHREAD=1:on-UNIX",
            "HAVE_SYSCONF=1:on-UNIX",
            "HAVE_TI_MODE=1:on-Clang-or-GNU",
            "-Wno-unknown-pragmas:on-Clang-or-GNU:upstream-policy",
            "-Wno-unused-function:on-Clang-or-GNU:upstream-policy",
        ],
    },
    "semver": {
        "version": "v0.3.1",
        "commit": "c333d59698765039d09e6b7bb41836886273cfaa",
        "archive_url": "https://github.com/Neargye/semver/archive/refs/tags/v0.3.1.tar.gz",
        "archive_sha256": "422b5882460a685a455fda9da53b85aa1824dcb9ba9dfbd0460ce50393f71061",
        "spdx": "MIT",
        "license": "LICENSE",
        "compile_options": ["header-only"],
    },
    "tomlplusplus": {
        "version": "v3.4.0",
        "commit": "30172438cee64926dc41fdd9c11fb3ba5b2ba9de",
        "archive_url": "https://github.com/marzer/tomlplusplus/archive/refs/tags/v3.4.0.tar.gz",
        "archive_sha256": "8517f65938a4faae9ccf8ebb36631a38c1cadfb5efa85d9a72e15b9e97d25155",
        "spdx": "MIT",
        "license": "LICENSE",
        "compile_options": ["header-only", "TOML_HEADER_ONLY=1"],
    },
    "zstd": {
        "version": "v1.5.7",
        "commit": "f8745da6ff1ad1e7bab384bd1f9d742439278e99",
        "archive_url": "https://github.com/facebook/zstd/archive/refs/tags/v1.5.7.tar.gz",
        "archive_sha256": "37d7284556b20954e56e1ca85b80226768902e2edabd3b649e9e72c0c9012ee3",
        "spdx": "BSD-3-Clause",
        "license": "LICENSE",
        "compile_options": [
            "C11",
            "static",
            "decompression-only",
            "ZSTD_DISABLE_ASM=1",
            "ZSTD_LEGACY_SUPPORT=0",
            "ZSTD_MULTITHREAD=0",
        ],
    },
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def encode_u32(value: int) -> bytes:
    return value.to_bytes(4, byteorder="big", signed=False)


def encode_u64(value: int) -> bytes:
    return value.to_bytes(8, byteorder="big", signed=False)


def inventory(dependency: str) -> tuple[list[dict[str, object]], str]:
    root = VENDOR_ROOT / dependency
    if not root.is_dir():
        raise ValueError(f"missing vendored dependency directory: {root.relative_to(ROOT)}")

    records: list[dict[str, object]] = []
    digest = hashlib.sha256()
    for path in sorted(root.rglob("*")):
        mode = path.lstat().st_mode
        if stat.S_ISLNK(mode) or not (stat.S_ISREG(mode) or stat.S_ISDIR(mode)):
            raise ValueError(f"non-regular vendored entry: {path.relative_to(ROOT)}")
        if not path.is_file():
            continue
        relative = path.relative_to(root).as_posix()
        data = path.read_bytes()
        path_bytes = relative.encode("utf-8")
        digest.update(encode_u32(len(path_bytes)))
        digest.update(path_bytes)
        digest.update(encode_u64(len(data)))
        digest.update(data)
        records.append({"path": relative, "size": len(data), "sha256": sha256(data)})
    return records, digest.hexdigest()


def enabled_sources(dependency: str, files: list[dict[str, object]]) -> list[str]:
    variable_spec = ENABLED_SOURCE_VARIABLES.get(dependency)
    if variable_spec is None:
        return []

    source_variable, root_variable, dependency_prefix = variable_spec
    cmake = CMAKE_PATH.read_text(encoding="utf-8")
    marker = f"set({source_variable}"
    start = cmake.find(marker)
    if start < 0:
        raise ValueError(f"missing CMake source inventory: {source_variable}")
    end = cmake.find(")", start)
    if end < 0:
        raise ValueError(f"unterminated CMake source inventory: {source_variable}")

    prefix = f"${{{root_variable}}}/"
    sources = []
    for token in cmake[start + len(marker) : end].split():
        if token.startswith(prefix) and token.endswith((".c", ".cc", ".cpp")):
            relative = token[len(prefix) :]
            sources.append(
                f"{dependency_prefix}/{relative}" if dependency_prefix else relative
            )

    if not sources:
        raise ValueError(f"empty CMake source inventory: {source_variable}")
    if len(sources) != len(set(sources)):
        raise ValueError(f"duplicate CMake source inventory entry: {source_variable}")

    admitted = {str(record["path"]) for record in files}
    missing = sorted(set(sources) - admitted)
    if missing:
        raise ValueError(
            f"CMake source inventory references unadmitted {dependency} paths: {missing}"
        )
    return sorted(sources)


def generated_manifest() -> dict[str, object]:
    dependencies: dict[str, object] = {}
    for name, metadata in sorted(DEPENDENCIES.items()):
        files, content_digest = inventory(name)
        license_path = VENDOR_ROOT / name / str(metadata["license"])
        if not license_path.is_file():
            raise ValueError(f"missing vendored license: {license_path.relative_to(ROOT)}")
        compiled_sources = enabled_sources(name, files)
        enabled_headers = [
            record["path"]
            for record in files
            if str(record["path"]).endswith((".h", ".hh", ".hpp"))
        ]
        dependencies[name] = {
            **metadata,
            "local_patch_sha256": EMPTY_SHA256,
            "extracted_content_sha256": content_digest,
            "enabled_sources": compiled_sources,
            "enabled_headers": enabled_headers,
            "files": files,
        }
    return {"version": 1, "dependencies": dependencies}


def canonical_json(value: dict[str, object]) -> str:
    return json.dumps(value, indent=2, sort_keys=True, ensure_ascii=True) + "\n"


def check() -> int:
    if not MANIFEST_PATH.is_file():
        print(f"error: missing {MANIFEST_PATH.relative_to(ROOT)}", file=sys.stderr)
        return 1
    try:
        actual = MANIFEST_PATH.read_text(encoding="utf-8")
        expected = canonical_json(generated_manifest())
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    if actual != expected:
        print(
            "error: vendored dependency inventory drift; run "
            "products/zomlang/tests/tools/check-vendored-dependencies.py --write",
            file=sys.stderr,
        )
        return 1
    print("vendored dependency inventory check passed")
    return 0


def write() -> int:
    try:
        manifest = canonical_json(generated_manifest())
        MANIFEST_PATH.write_text(manifest, encoding="utf-8", newline="\n")
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    print(f"wrote {MANIFEST_PATH.relative_to(ROOT)}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify RFC 0012 vendored dependencies")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true", help="verify the checked inventory")
    mode.add_argument("--write", action="store_true", help="regenerate the checked inventory")
    args = parser.parse_args()
    return write() if args.write else check()


if __name__ == "__main__":
    raise SystemExit(main())
