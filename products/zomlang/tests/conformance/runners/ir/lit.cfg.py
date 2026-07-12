#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Lit configuration for checked ZOM IR conformance expectations."""

import os
import platform
import tempfile

import lit.formats
import lit.util

config.name = "ZOM-IR-Conformance"
config.test_format = lit.formats.ShTest(True)
config.suffixes = [".check"]

runner_root = os.path.dirname(__file__)
conformance_root = os.path.abspath(os.path.join(runner_root, "..", ".."))
test_root = os.path.abspath(os.path.join(conformance_root, "expectations", "ir"))
corpus_root = os.path.abspath(os.path.join(conformance_root, "corpus"))
repo_root = os.path.abspath(os.path.join(conformance_root, "..", "..", "..", ".."))

config.test_source_root = test_root
base_test_exec_root = os.path.join(runner_root, "Output")
os.makedirs(base_test_exec_root, exist_ok=True)
config.test_exec_root = tempfile.mkdtemp(prefix="lit-", dir=base_test_exec_root)

config.excludes = ["CMakeLists.txt", "README.md", "Output", "lit.cfg.py"]
config.available_features.add("zom-ir-tests")

if platform.system() == "Darwin":
    config.available_features.add("darwin")
elif platform.system() == "Linux":
    config.available_features.add("linux")
elif platform.system() == "Windows":
    config.available_features.add("windows")

zomc_path = lit.util.which("zomc")
if not zomc_path:
    cmake_binary_dir = os.environ.get("CMAKE_BINARY_DIR", "")
    if cmake_binary_dir:
        potential_path = os.path.join(
            cmake_binary_dir, "products", "zomlang", "utils", "zomc", "zomc"
        )
        if os.path.exists(potential_path):
            zomc_path = potential_path

    if not zomc_path:
        for build_dir in ["build-sanitizer", "build", "build-debug", "build-release"]:
            potential_path = os.path.join(
                repo_root, build_dir, "products", "zomlang", "utils", "zomc", "zomc"
            )
            if os.path.exists(potential_path):
                zomc_path = potential_path
                break

if not zomc_path:
    lit_config.fatal("Could not find zomc compiler")

filecheck_path = os.path.join(
    repo_root, "products", "zomlang", "tests", "tools", "filecheck.py"
)
package_runner_path = os.path.join(
    repo_root, "products", "zomlang", "tests", "tools", "run-zomc-package.py"
)
zomc_command = f"python3 {package_runner_path} --zomc {zomc_path}"

config.substitutions.append(("%zomc", zomc_command))
config.substitutions.append(("%corpus", corpus_root))
config.substitutions.append(("%t", os.path.join(config.test_exec_root, "temp")))
config.substitutions.append(("%FileCheck", f"python3 {filecheck_path}"))

config.environment["ZOMLANG_TEST_ROOT"] = conformance_root
config.environment["ZOMLANG_CORPUS_ROOT"] = corpus_root
config.environment["ZOMLANG_BUILD_ROOT"] = os.environ.get("CMAKE_BINARY_DIR", "")

if os.environ.get("ZOM_ENABLE_COVERAGE"):
    coverage_dir = os.path.join(os.environ.get("CMAKE_BINARY_DIR", ""), "coverage")
    os.makedirs(coverage_dir, exist_ok=True)
    profile_path = os.path.join(coverage_dir, "ir-lit-%m.profraw")
    for index, (pattern, _) in enumerate(config.substitutions):
        if pattern == "%zomc":
            config.substitutions[index] = (
                "%zomc",
                f"LLVM_PROFILE_FILE={profile_path} ZC_CLEAN_SHUTDOWN=1 {zomc_command}",
            )
            break
    lit_config.note(f"Coverage enabled: LLVM_PROFILE_FILE={profile_path}")

lit_config.maxIndividualTestTime = 20
lit_config.parallelism_group = "zomlang-ir"
