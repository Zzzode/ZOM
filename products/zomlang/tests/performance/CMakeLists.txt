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

# Performance Tests for ZomLang Compiler
# These tests measure and verify compiler performance characteristics

# Only build performance tests if explicitly enabled
if(NOT ZOM_ENABLE_PERFORMANCE_TESTS)
  return()
endif()

# Lexer Performance Tests
add_executable(performance-lexer-benchmark
  lexer-benchmark.cc
)

target_link_libraries(performance-lexer-benchmark PRIVATE
  frontend
  ztest
)

target_include_directories(performance-lexer-benchmark PRIVATE
  ${ZOM_ROOT}/libraries
  ${ZOM_ROOT}/products
)

add_performance_test("lexer-benchmark" performance-lexer-benchmark)

# Parser Performance Tests
add_executable(performance-parser-benchmark
  parser-benchmark.cc
)

target_link_libraries(performance-parser-benchmark PRIVATE
  frontend
  ztest
)

target_include_directories(performance-parser-benchmark PRIVATE
  ${ZOM_ROOT}/libraries
  ${ZOM_ROOT}/products
)

add_performance_test("parser-benchmark" performance-parser-benchmark)

# Memory Usage Tests
add_executable(performance-memory-usage
  memory-usage-test.cc
)

target_link_libraries(performance-memory-usage PRIVATE
  frontend
  ztest
)

target_include_directories(performance-memory-usage PRIVATE
  ${ZOM_ROOT}/libraries
  ${ZOM_ROOT}/products
)

add_performance_test("memory-usage" performance-memory-usage)

# Compilation Speed Tests
add_executable(performance-compilation-speed
  compilation-speed-test.cc
)

target_link_libraries(performance-compilation-speed PRIVATE
  frontend
  ztest
)

target_include_directories(performance-compilation-speed PRIVATE
  ${ZOM_ROOT}/libraries
  ${ZOM_ROOT}/products
)

add_performance_test("compilation-speed" performance-compilation-speed)

# Large File Handling Tests
add_executable(performance-large-files
  large-files-test.cc
)

target_link_libraries(performance-large-files PRIVATE
  frontend
  ztest
)

target_include_directories(performance-large-files PRIVATE
  ${ZOM_ROOT}/libraries
  ${ZOM_ROOT}/products
)

add_performance_test("large-files" performance-large-files)