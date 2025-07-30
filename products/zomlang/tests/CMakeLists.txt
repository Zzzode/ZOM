# Copyright (c) 2025 Zode.Z. All rights reserved
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations under
# the License.

# ZomLang Compiler Test Suite
# This directory contains all tests for the ZomLang compiler

# Enable testing
enable_testing()

# Unit Tests - Low-level component tests
add_subdirectory(unittests)

# Language Tests - Language specification compliance tests
add_subdirectory(language)

# Performance Tests - Benchmarks and performance regression tests
if(ZOM_ENABLE_PERFORMANCE_TESTS)
  add_subdirectory(performance)
endif()

# Regression Tests - Tests for previously fixed bugs
add_subdirectory(regression)

# Custom test targets
add_custom_target(check-unit
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -R "unit-"
  COMMENT "Running unit tests"
)

add_custom_target(check-integration
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -R "integration-"
  COMMENT "Running integration tests"
)

add_custom_target(check-language
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -R "language-"
  COMMENT "Running language specification tests"
)

add_custom_target(check-regression
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -R "regression-"
  COMMENT "Running regression tests"
)

add_custom_target(check-all
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
  COMMENT "Running all tests"
)

# Add dependencies
add_dependencies(check-unit zomc)
add_dependencies(check-integration zomc)
add_dependencies(check-language zomc)
add_dependencies(check-regression zomc)
add_dependencies(check-all zomc)