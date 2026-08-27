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

/// \file lexer-fuzzer.cc
/// \brief LibFuzzer target for the ZOM lexer.
///
/// Build with:
///   clang++ -std=c++20 -fsanitize=fuzzer,address,undefined \
///     -I<zom-include-paths> lexer-fuzzer.cc -l<zom-libs> -o lexer-fuzzer
///
/// Run with:
///   ./lexer-fuzzer corpus/lexer/ -max_len=4096

#include <cstdint>
#include <cstring>
#include <string_view>

#include "zc/core/string.h"
#include "compiler/basic/string-pool.h"
#include "compiler/basic/zomlang-opts.h"
#include "compiler/diagnostics/core/diagnostic-engine.h"
#include "compiler/lexer/lexer.h"
#include "compiler/lexer/token.h"
#include "compiler/source/manager.h"

using namespace zomlang::compiler;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  // Reject inputs that are not valid UTF-8 prefixes — the lexer assumes UTF-8.
  // We still want to test malformed UTF-8 detection, so we only skip empty.
  if (size == 0) { return 0; }

  basic::LangOptions opts;
  source::SourceManager sourceMgr;
  diagnostics::DiagnosticEngine diagEngine(sourceMgr);
  basic::StringPool stringPool;

  // Create a string from the fuzz input.
  zc::String source(reinterpret_cast<const char*>(data), size);
  auto bufferId = sourceMgr.addMemBuffer(zc::Str("fuzz.zom"), source.asStringPtr());

  Lexer lexer(sourceMgr, diagEngine, opts, stringPool, bufferId);
  Token token;
  int tokenCount = 0;
  do {
    lexer.lex(token);
    ++tokenCount;
    // Cap token count to avoid infinite-loop hangs on adversarial inputs.
    if (tokenCount > 100000) { break; }
  } while (!token.is(ast::SyntaxKind::EndOfFile));

  return 0;
}
