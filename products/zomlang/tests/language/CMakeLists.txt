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

# Language Specification Tests for ZomLang
# These tests verify that the compiler correctly handles ZomLang language constructs
# according to the language specification

# Lexical Analysis Tests
add_subdirectory(lexical)

# Expression Tests
add_subdirectory(expressions)

# Statement Tests
add_subdirectory(statements)

# Declaration Tests
add_subdirectory(declarations)

# Type System Tests
add_subdirectory(types)

# Class Tests
add_subdirectory(classes)

# Interface Tests
add_subdirectory(interfaces)

# Module Tests
add_subdirectory(modules)

# Error Handling Tests
add_subdirectory(errors)

# Semantic Analysis Tests
add_subdirectory(semantic)
