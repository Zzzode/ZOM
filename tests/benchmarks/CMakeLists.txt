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

# ZomLang Compiler Benchmarks
#
# These are standalone micro-benchmarks for the compiler frontend.
# They are NOT built as part of the normal test suite — build them
# explicitly with:
#
#   cmake --build <build-dir> --target zom-parser-benchmark
#
# Or enable with -DZOM_ENABLE_BENCHMARKS=ON.

option(ZOM_ENABLE_BENCHMARKS "Build compiler frontend benchmarks" OFF)

if(ZOM_ENABLE_BENCHMARKS)
  add_executable(zom-parser-benchmark
    compiler/parser-benchmark.cc
  )

  target_link_libraries(zom-parser-benchmark PRIVATE frontend)

  target_include_directories(zom-parser-benchmark PRIVATE
    ${CMAKE_SOURCE_DIR}
  )
endif()
