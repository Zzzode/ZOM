#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Lit configuration for ZomLang AST tests.

This configuration sets up the lit testing framework for ZomLang compiler
AST dump tests, following LLVM/Clang best practices.
"""

import os
import platform
import re
import subprocess
import tempfile

import lit.formats
import lit.util

from lit.llvm import llvm_config
from lit.llvm.subst import ToolSubst
from lit.llvm.subst import FindTool

# Configuration file for the 'lit' test runner.

# name: The name of this test suite.
config.name = "ZomLang-AST"

# testFormat: The test format to use to interpret tests.
config.test_format = lit.formats.ShTest(True)

# suffixes: A list of file extensions to treat as test files.
config.suffixes = [".zom"]

# test_source_root: The root path where tests are located.
config.test_source_root = os.path.dirname(__file__)

# test_exec_root: The root path where tests should be run.
config.test_exec_root = os.path.join(config.test_source_root, "Output")

# Ensure the output directory exists
os.makedirs(config.test_exec_root, exist_ok=True)

# excludes: A list of directories to exclude from the testsuite.
config.excludes = ["CMakeLists.txt", "README.md", "Output"]

# available_features: A list of features that can be used in REQUIRES and UNSUPPORTED directives.
config.available_features.add("zom-ast-tests")

# Add platform-specific features
if platform.system() == "Darwin":
    config.available_features.add("darwin")
elif platform.system() == "Linux":
    config.available_features.add("linux")
elif platform.system() == "Windows":
    config.available_features.add("windows")

# Find zomc compiler
zomc_path = lit.util.which("zomc")
if not zomc_path:
    # Try to find zomc in current build directory from CMAKE_BINARY_DIR
    cmake_binary_dir = os.environ.get("CMAKE_BINARY_DIR", "")
    if cmake_binary_dir:
        potential_path = os.path.join(
            cmake_binary_dir,
            "products",
            "zomlang",
            "utils",
            "zomc",
            "zomc",
        )
        if os.path.exists(potential_path):
            zomc_path = potential_path

    # Fallback: try common build directories if CMAKE_BINARY_DIR is not set
    if not zomc_path:
        build_dirs = ["build-sanitizer", "build", "build-debug", "build-release"]
        for build_dir in build_dirs:
            potential_path = os.path.join(
                config.test_source_root,
                "..",
                "..",
                "..",
                build_dir,
                "products",
                "zomlang",
                "utils",
                "zomc",
                "zomc",
            )
            if os.path.exists(potential_path):
                zomc_path = potential_path
                break

if not zomc_path:
    lit_config.fatal("Could not find zomc compiler")

# FileCheck tool (we'll implement a simple version if not available)
filecheck_path = lit.util.which("FileCheck", config.environment.get("PATH", ""))
if not filecheck_path:
    # Use our custom FileCheck implementation
    filecheck_path = os.path.join(config.test_source_root, "tools", "filecheck.py")

# Tool substitutions
config.substitutions.append(("%zomc", zomc_path))
# %s is automatically replaced by lit with the test file path
config.substitutions.append(("%t", os.path.join(config.test_exec_root, "temp")))
config.substitutions.append(("%FileCheck", f"python3 {filecheck_path}"))

# Set environment variables
config.environment["ZOMLANG_TEST_ROOT"] = config.test_source_root
config.environment["ZOMLANG_BUILD_ROOT"] = os.environ.get("CMAKE_BINARY_DIR", "")

# Timeout for individual tests (in seconds)
lit_config.maxIndividualTestTime = 60

# Enable parallel execution
lit_config.parallelism_group = "zomlang-ast"
