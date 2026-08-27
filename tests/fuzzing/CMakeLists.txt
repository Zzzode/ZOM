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

# ZomLang Compiler Fuzz Targets
#
# LibFuzzer targets for the lexer and parser. These require a compiler with
# libFuzzer support (Clang). Build with:
#
#   cmake -B build-fuzz -DZOM_ENABLE_FUZZING=ON \
#     -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
#     -DCMAKE_CXX_FLAGS="-fsanitize=fuzzer-no-link,address,undefined"
#   cmake --build build-fuzz --target zom-lexer-fuzzer zom-parser-fuzzer
#
# Then run:
#   ./zom-lexer-fuzzer tests/fuzzing/corpus/lexer/
#   ./zom-parser-fuzzer tests/fuzzing/corpus/parser/

option(ZOM_ENABLE_FUZZING "Build libFuzzer targets for the compiler" OFF)

if(ZOM_ENABLE_FUZZING)
  # Lexer fuzzer
  add_executable(zom-lexer-fuzzer lexer-fuzzer.cc)
  target_link_libraries(zom-lexer-fuzzer PRIVATE frontend)
  target_include_directories(zom-lexer-fuzzer PRIVATE ${CMAKE_SOURCE_DIR})

  # Parser fuzzer
  add_executable(zom-parser-fuzzer parser-fuzzer.cc)
  target_link_libraries(zom-parser-fuzzer PRIVATE frontend)
  target_include_directories(zom-parser-fuzzer PRIVATE ${CMAKE_SOURCE_DIR})
endif()
