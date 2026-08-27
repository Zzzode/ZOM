# RFC 0016 -- LLVM discovery gate configure-negative/positive fixture harness.
#
# This project intentionally contains no compiler target. It includes the real
# repository gate module (cmake/utils/llvm.cmake) so that every fixture exercises
# the exact top-level fail-closed contract and fails BEFORE any compiler target
# could be generated. The Python driver
# (products/zomlang/tests/tools/check-llvm-discovery-gate.py) invokes this
# harness once per case with an isolated build directory and asserts the stable
# repository-owned failure identifier (or the passing provenance record).

cmake_minimum_required(VERSION 3.25.0)

project(zom-llvm-discovery-gate NONE)

if(NOT DEFINED ZOM_GATE_MODULE OR "${ZOM_GATE_MODULE}" STREQUAL "")
  message(FATAL_ERROR "Fixture harness requires -DZOM_GATE_MODULE=<path to cmake/utils/llvm.cmake>")
endif()

# The gate module runs unconditionally here; the harness stands in for the
# top-level ZOM_ENABLE_LLVM_BACKEND=ON include site.
include("${ZOM_GATE_MODULE}")

# A marker that only prints if the gate above admitted the install. No negative
# fixture may ever reach this line.
message(STATUS "ZOM-CMAKE-LLVM-GATE-FIXTURE: admitted")
