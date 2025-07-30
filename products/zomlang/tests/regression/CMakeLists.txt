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

# Regression Tests for ZomLang Compiler
# These tests ensure that previously fixed bugs do not reappear

# Each regression test should be named with the issue/bug number it addresses
# Format: regression-issue-XXXX or regression-bug-XXXX

# Example regression tests (uncomment and modify as needed)

# Regression test for issue #001 - Parser crash on empty function body
# add_regression_test("issue-001-empty-function" "issue-001/empty-function.zom" "001")

# Regression test for issue #002 - Lexer infinite loop on unterminated string
# add_regression_test("issue-002-unterminated-string" "issue-002/unterminated-string.zom" "002")

# Regression test for issue #003 - AST dumper segfault on nested expressions
# add_regression_test("issue-003-nested-expressions" "issue-003/nested-expressions.zom" "003")

# Directory structure for regression tests:
# regression/
# ├── issue-001/
# │   ├── empty-function.zom
# │   └── empty-function.expected (optional)
# ├── issue-002/
# │   ├── unterminated-string.zom
# │   └── unterminated-string.expected (optional)
# └── ...

# Automatically discover regression test files (example)
file(GLOB ISSUE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/issue-*")

foreach(ISSUE_DIR ${ISSUE_DIRS})
  if(IS_DIRECTORY ${ISSUE_DIR})
    get_filename_component(ISSUE_NAME ${ISSUE_DIR} NAME)
    
    # Extract issue number from directory name
    string(REGEX MATCH "issue-([0-9]+)" ISSUE_MATCH ${ISSUE_NAME})
    if(ISSUE_MATCH)
      set(ISSUE_NUMBER ${CMAKE_MATCH_1})
      
      # Find all .zom files in the issue directory
      file(GLOB ZOM_FILES "${ISSUE_DIR}/*.zom")
      
      foreach(ZOM_FILE ${ZOM_FILES})
        get_filename_component(TEST_NAME ${ZOM_FILE} NAME_WE)
        set(FULL_TEST_NAME "${ISSUE_NAME}-${TEST_NAME}")
        
        add_regression_test(${FULL_TEST_NAME} ${ZOM_FILE} ${ISSUE_NUMBER})
      endforeach()
    endif()
  endif()
endforeach()
