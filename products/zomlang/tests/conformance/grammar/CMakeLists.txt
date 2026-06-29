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

find_program(BASH_EXECUTABLE NAMES bash)
if(NOT BASH_EXECUTABLE)
  message(FATAL_ERROR "bash is required for grammar conformance tests")
endif()

add_test(
  NAME conformance-grammar
  COMMAND ${BASH_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run_tests.sh
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

set_tests_properties(conformance-grammar PROPERTIES
  LABELS "conformance;conformance-grammar;grammar"
  TIMEOUT 900
  ENVIRONMENT "ZOM_CONFORMANCE_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}/antlr"
)
