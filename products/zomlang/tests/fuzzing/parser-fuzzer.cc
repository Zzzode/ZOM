// Copyright (c) 2024-2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

/// \file parser-fuzzer.cc
/// \brief LibFuzzer target for the ZOM parser.
///
/// Build with:
///   clang++ -std=c++20 -fsanitize=fuzzer,address,undefined \
///     -I<zom-include-paths> parser-fuzzer.cc -l<zom-libs> -o parser-fuzzer
///
/// Run with:
///   ./parser-fuzzer corpus/parser/ -max_len=8192

#include <cstdint>
#include <cstring>

#include "zc/core/string.h"
#include "zomlang/compiler/ast/schema-verifier.h"
#include "zomlang/compiler/basic/string-pool.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/diagnostics/source-diagnostic-draft-buffer.h"
#include "zomlang/compiler/parser/parser.h"
#include "zomlang/compiler/source/manager.h"

using namespace zomlang::compiler;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size == 0) { return 0; }

  basic::LangOptions opts;
  source::SourceManager sourceMgr;
  basic::StringPool stringPool;

  const auto source = zc::ArrayPtr<const zc::byte>(reinterpret_cast<const zc::byte*>(data), size);
  const auto bufferId = sourceMgr.addMemBufferCopy(source, "fuzz.zom");

  diagnostics::SourceDiagnosticDraftBuffer diagnosticFacts(sourceMgr, bufferId);
  parser::Parser sourceParser(sourceMgr, diagnosticFacts, opts, stringPool, bufferId);
  ZC_IF_SOME(tree, sourceParser.parse()) {
    // Exercise tree verification to catch schema violations from fuzz input.
    (void)ast::verifySchemaFailure(tree);
  }

  return 0;
}
