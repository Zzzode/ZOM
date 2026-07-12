# Copyright (c) 2026 Zode.Z. All rights reserved
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

find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(PACKAGE_RESOLVER_EDGE_FIXTURE
    "${CMAKE_BINARY_DIR}/generated/package-resolver-performance-edges.bin")

add_custom_command(
  OUTPUT "${PACKAGE_RESOLVER_EDGE_FIXTURE}"
  COMMAND
    ${Python3_EXECUTABLE}
    "${ZOM_ROOT}/scripts/codegen/gen_package_resolver_performance_edges.py"
    --output "${PACKAGE_RESOLVER_EDGE_FIXTURE}" --packages 10000 --edges 50000
  DEPENDS
    "${ZOM_ROOT}/scripts/codegen/gen_package_resolver_performance_edges.py"
  COMMENT "Generating canonical RFC 0012 resolver performance edges"
  VERBATIM)

add_custom_target(package-resolver-performance-fixture
                  DEPENDS "${PACKAGE_RESOLVER_EDGE_FIXTURE}")
add_executable(package-resolver-performance-test package-resolver-performance.cc)
add_dependencies(package-resolver-performance-test
                 package-resolver-performance-fixture)
target_link_libraries(package-resolver-performance-test PRIVATE frontend ztest)
target_include_directories(package-resolver-performance-test
                           PRIVATE ${ZOM_ROOT}/libraries ${ZOM_ROOT}/products)
target_compile_options(package-resolver-performance-test
                       PRIVATE -Wno-global-constructors)
target_compile_definitions(
  package-resolver-performance-test
  PRIVATE
    ZOM_PACKAGE_RESOLVER_PERFORMANCE_FIXTURE="${PACKAGE_RESOLVER_EDGE_FIXTURE}")
add_test(
  NAME performance-package-resolver
  COMMAND
    ${Python3_EXECUTABLE}
    "${ZOM_ROOT}/scripts/run_package_resolver_performance.py" --executable
    $<TARGET_FILE:package-resolver-performance-test> --max-rss-bytes 1073741824)
set_tests_properties(
  performance-package-resolver
  PROPERTIES LABELS "performance;benchmark"
             TIMEOUT 300)
