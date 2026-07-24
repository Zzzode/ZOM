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

set(DIAGNOSTIC_EXPECTATION_ROOT
  ${CMAKE_SOURCE_DIR}/products/zomlang/tests/conformance/expectations/diagnostics
)

set(DIAGNOSTIC_TEST_ENV "CMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}")
if(ZOM_ENABLE_COVERAGE)
  string(APPEND DIAGNOSTIC_TEST_ENV ";ZOM_ENABLE_COVERAGE=ON")
endif()

add_test(
  NAME conformance-diagnostics
  COMMAND ${ZOM_LIT_COMMAND} -v -j 3 --timeout=90 ${DIAGNOSTIC_EXPECTATION_ROOT}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/products/zomlang/tests
)

set_tests_properties(conformance-diagnostics PROPERTIES
  LABELS "conformance;conformance-diagnostics;diagnostics;lit;filecheck;specification"
  TIMEOUT 300
  PROCESSORS 3
  ENVIRONMENT "${DIAGNOSTIC_TEST_ENV}"
)
