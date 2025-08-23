#!/usr/bin/env python3
"""
Sync KJ files from capnproto to ZC library
This script clones capnproto and copies KJ files to ZC with appropriate renaming.
"""

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

# Constants
ZC_ROOT = Path(__file__).parent.parent / "zc"
CAPNP_REPO = "https://github.com/capnproto/capnproto.git"
KJ_SOURCE_DIR = "c++/src/kj"
TEMP_DIR = Path("/tmp/kj-sync")

# Source extensions
SOURCE_EXTENSIONS = {".h", ".c++"}


def clone_capnp():
    """Clone capnproto repository"""
    if TEMP_DIR.exists():
        shutil.rmtree(TEMP_DIR)

    TEMP_DIR.mkdir(parents=True, exist_ok=True)

    print("Cloning capnproto...")
    subprocess.run(
        ["git", "clone", "--depth", "1", CAPNP_REPO, str(TEMP_DIR / "capnproto")],
        check=True,
    )

    return TEMP_DIR / "capnproto"


def get_all_kj_files(kj_source_path):
    """Get all .h and .c++ files from kj source directory"""
    all_files = set()

    for root, dirs, files in os.walk(kj_source_path):
        for file in files:
            if file.endswith((".h", ".c++")):
                rel_path = os.path.relpath(os.path.join(root, file), kj_source_path)
                all_files.add(rel_path)

    return all_files


def validate_file_mapping(all_kj_files, file_mapping):
    """Validate file mapping completeness"""
    mapped_files = set(file_mapping.keys())

    # Check for missing mappings
    missing = all_kj_files - mapped_files
    if missing:
        print("ERROR: Missing file mappings for:")
        for f in sorted(missing):
            print(f"  - {f}")
        sys.exit(1)

    # Check for extra mappings
    extra = mapped_files - all_kj_files
    if extra:
        print("ERROR: Extra file mappings (files not found):")
        for f in sorted(extra):
            print(f"  - {f}")
        sys.exit(1)

    print("✓ File mapping validation passed")


# 精确的文件映射表
file_mapping = {
    # ==============================================================================
    # Source Files
    # ==============================================================================
    # Root level files
    "arena.h": "core/arena.h",
    "arena.c++": "core/arena.cc",
    "array.h": "core/array.h",
    "array.c++": "core/array.cc",
    "async.h": "async/async.h",
    "async.c++": "async/async.cc",
    "async-inl.h": "async/async-inl.h",
    "async-prelude.h": "async/async-prelude.h",
    "async-io.h": "async/async-io.h",
    "async-io.c++": "async/async-io.cc",
    "async-io-internal.h": "async/async-io-internal.h",
    "async-io-unix.c++": "async/async-io-unix.cc",
    "async-io-win32.c++": "async/async-io-win32.cc",
    "async-queue.h": "async/async-queue.h",
    "async-unix.h": "async/async-unix.h",
    "async-unix.c++": "async/async-unix.cc",
    "async-win32.h": "async/async-win32.h",
    "async-win32.c++": "async/async-win32.cc",
    "timer.h": "async/timer.h",
    "timer.c++": "async/timer.cc",
    "cidr.h": "core/cidr.h",
    "cidr.c++": "core/cidr.cc",
    "common.h": "core/common.h",
    "common.c++": "core/common.cc",
    "debug.h": "core/debug.h",
    "debug.c++": "core/debug.cc",
    "encoding.h": "core/encoding.h",
    "encoding.c++": "core/encoding.cc",
    "exception.h": "core/exception.h",
    "exception.c++": "core/exception.cc",
    "filesystem.h": "core/filesystem.h",
    "filesystem.c++": "core/filesystem.cc",
    "filesystem-disk-unix.c++": "core/filesystem-disk-unix.cc",
    "filesystem-disk-win32.c++": "core/filesystem-disk-win32.cc",
    "function.h": "core/function.h",
    "glob-filter.h": "core/glob-filter.h",
    "glob-filter.c++": "core/glob-filter.cc",
    "hash.h": "core/hash.h",
    "hash.c++": "core/hash.cc",
    "io.h": "core/io.h",
    "io.c++": "core/io.cc",
    "list.h": "core/list.h",
    "list.c++": "core/list.cc",
    "main.h": "core/main.h",
    "main.c++": "core/main.cc",
    "map.h": "core/map.h",
    "memory.h": "core/memory.h",
    "memory.c++": "core/memory.cc",
    "miniposix.h": "core/miniposix.h",
    "mutex.h": "core/mutex.h",
    "mutex.c++": "core/mutex.cc",
    "one-of.h": "core/one-of.h",
    "refcount.h": "core/refcount.h",
    "refcount.c++": "core/refcount.cc",
    "source-location.h": "core/source-location.h",
    "source-location.c++": "core/source-location.cc",
    "string.h": "core/string.h",
    "string.c++": "core/string.cc",
    "string-tree.h": "core/string-tree.h",
    "string-tree.c++": "core/string-tree.cc",
    "table.h": "core/table.h",
    "table.c++": "core/table.cc",
    "thread.h": "core/thread.h",
    "thread.c++": "core/thread.cc",
    "time.h": "core/time.h",
    "time.c++": "core/time.cc",
    "tuple.h": "core/tuple.h",
    "units.h": "core/units.h",
    "units.c++": "core/units.cc",
    "vector.h": "core/vector.h",
    "win32-api-version.h": "core/win32-api-version.h",
    "windows-sanity.h": "core/windows-sanity.h",
    "test.h": "ztest/test.h",
    "test.c++": "ztest/test.cc",
    "test-helpers.c++": "ztest/test-helpers.cc",
    # compat/ -> zc/
    "compat/brotli.h": "zip/brotli.h",
    "compat/brotli.c++": "zip/brotli.cc",
    "compat/gzip.h": "zip/gzip.h",
    "compat/gzip.c++": "zip/gzip.cc",
    "compat/http.h": "http/http.h",
    "compat/http.c++": "http/http.cc",
    "compat/url.h": "http/url.h",
    "compat/url.c++": "http/url.cc",
    "compat/readiness-io.h": "tls/readiness-io.h",
    "compat/readiness-io.c++": "tls/readiness-io.cc",
    "compat/tls.h": "tls/tls.h",
    "compat/tls.c++": "tls/tls.cc",
    "compat/gtest.h": "ztest/gtest.h",
    # parse/ -> zc/parse/
    "parse/char.h": "parse/char.h",
    "parse/char.c++": "parse/char.cc",
    "parse/common.h": "parse/common.h",
    # std/ -> zc/core/
    "std/iostream.h": "core/iostream.h",
    # ==============================================================================
    # Test Files
    # ==============================================================================
    "arena-test.c++": "unittests/core/arena-test.cc",
    "array-test.c++": "unittests/core/array-test.cc",
    "async-coroutine-test.c++": "unittests/async/async-coroutine-test.cc",
    "async-io-test.c++": "unittests/async/async-io-test.cc",
    "async-queue-test.c++": "unittests/async/async-queue-test.cc",
    "async-test.c++": "unittests/async/async-test.cc",
    "async-unix-test.c++": "unittests/async/async-unix-test.cc",
    "async-unix-xthread-test.c++": "unittests/async/async-unix-xthread-test.cc",
    "async-win32-test.c++": "unittests/async/async-win32-test.cc",
    "async-win32-xthread-test.c++": "unittests/async/async-win32-xthread-test.cc",
    "async-xthread-test.c++": "unittests/async/async-xthread-test.cc",
    "common-test.c++": "unittests/core/common-test.cc",
    "debug-test.c++": "unittests/core/debug-test.cc",
    "encoding-test.c++": "unittests/core/encoding-test.cc",
    "exception-override-symbolizer-test.c++": "unittests/core/exception-override-symbolizer-test.cc",
    "exception-test.c++": "unittests/core/exception-test.cc",
    "filesystem-disk-generic-test.c++": "unittests/core/filesystem-disk-generic-test.cc",
    "filesystem-disk-old-kernel-test.c++": "unittests/core/filesystem-disk-old-kernel-test.cc",
    "filesystem-disk-test.c++": "unittests/core/filesystem-disk-test.cc",
    "filesystem-test.c++": "unittests/core/filesystem-test.cc",
    "function-test.c++": "unittests/core/function-test.cc",
    "glob-filter-test.c++": "unittests/core/glob-filter-test.cc",
    "io-test.c++": "unittests/core/io-test.cc",
    "list-test.c++": "unittests/core/list-test.cc",
    "map-test.c++": "unittests/core/map-test.cc",
    "memory-test.c++": "unittests/core/memory-test.cc",
    "mutex-test.c++": "unittests/core/mutex-test.cc",
    "one-of-test.c++": "unittests/core/one-of-test.cc",
    "refcount-test.c++": "unittests/core/refcount-test.cc",
    "string-test.c++": "unittests/core/string-test.cc",
    "string-tree-test.c++": "unittests/core/string-tree-test.cc",
    "table-test.c++": "unittests/core/table-test.cc",
    "test-test.c++": "unittests/ztest/test-test.cc",
    "thread-test.c++": "unittests/core/thread-test.cc",
    "time-test.c++": "unittests/core/time-test.cc",
    "tuple-test.c++": "unittests/core/tuple-test.cc",
    "units-test.c++": "unittests/core/units-test.cc",
    # compat test files
    "compat/brotli-test.c++": "unittests/zip/brotli-test.cc",
    "compat/gzip-test.c++": "unittests/zip/gzip-test.cc",
    "compat/http-socketpair-test.c++": "unittests/http/http-socketpair-test.cc",
    "compat/http-test.c++": "unittests/http/http-test.cc",
    "compat/readiness-io-test.c++": "unittests/tls/readiness-io-test.cc",
    "compat/tls-test.c++": "unittests/tls/tls-test.cc",
    "compat/url-test.c++": "unittests/http/url-test.cc",
    # parse test files
    "parse/char-test.c++": "unittests/parse/char-test.cc",
    "parse/common-test.c++": "unittests/parse/common-test.cc",
    # std test files
    "std/iostream-test.c++": "unittests/core/iostream-test.cc",
}


def get_file_mapping(kj_source_path):
    """Map kj files to zc target directories with precise mapping"""
    mapping = {}

    # 获取capnp中所有文件
    all_kj_files = get_all_kj_files(kj_source_path)

    # 验证文件映射完整性
    validate_file_mapping(all_kj_files, file_mapping)

    # 使用验证后的精确映射构建实际文件映射
    for rel_path, target_rel_path in file_mapping.items():
        source_file = kj_source_path / rel_path
        target_path = ZC_ROOT / target_rel_path
        mapping[source_file] = target_path

    return mapping


def process_file_content(content):
    """Process file content: 1. replace KJ->ZC, kj->zc 2. replace include files"""
    content = content.replace("KJ", "ZC")
    content = content.replace("kj", "zc")

    include_map = {
        src: f"zc/{dst}" for src, dst in file_mapping.items() if src.endswith(".h")
    }

    for src_key, target_path in include_map.items():
        pattern1 = rf'#include ["<]{re.escape(src_key)}[">]'
        content = re.sub(pattern1, f'#include "{target_path}"', content)

        pattern2 = rf'#include ["<]zc/{re.escape(src_key)}[">]'
        content = re.sub(pattern2, f'#include "{target_path}"', content)

    return content


def copy_and_process_files(file_mapping):
    """Copy and process files from kj to zc"""
    print("Copying and processing files...")

    for source, target in file_mapping.items():
        # Create target directory if it doesn't exist
        target.parent.mkdir(parents=True, exist_ok=True)

        # Read source file
        with open(source, "r", encoding="utf-8", errors="ignore") as f:
            content = f.read()

        # Process content
        processed_content = process_file_content(content)

        # Write to target
        with open(target, "w", encoding="utf-8") as f:
            f.write(processed_content)

        print(f"Copied: {source} -> {target}")


def format_code():
    """Run clang-format on updated files"""
    print("Formatting code...")

    # Find all .h and .cc files in zc directory
    for root, dirs, files in os.walk(ZC_ROOT):
        for file in files:
            if file.endswith((".h", ".cc")):
                file_path = Path(root) / file
                try:
                    subprocess.run(
                        ["clang-format", "-i", str(file_path)],
                        check=True,
                        capture_output=True,
                    )
                except subprocess.CalledProcessError as e:
                    print(f"Warning: Failed to format {file_path}: {e}")


def main():
    """Main sync process"""
    try:
        # Clone capnproto
        capnp_path = clone_capnp()

        print("Starting sync...")

        # Get kj source path
        kj_source_path = capnp_path / KJ_SOURCE_DIR

        # Map files
        file_mapping = get_file_mapping(kj_source_path)

        # Copy and process files
        copy_and_process_files(file_mapping)

        # Format code
        format_code()

        print("Sync completed successfully!")

    except Exception as e:
        print(f"Error during sync: {e}")
        raise
    finally:
        # Clean up temp directory
        if TEMP_DIR.exists():
            shutil.rmtree(TEMP_DIR)


if __name__ == "__main__":
    main()
