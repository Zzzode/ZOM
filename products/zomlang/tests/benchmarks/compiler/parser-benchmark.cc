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

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "zc/core/string.h"
#include "zomlang/compiler/basic/string-pool.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/diagnostics/source-diagnostic-draft-buffer.h"
#include "zomlang/compiler/lexer/lexer.h"
#include "zomlang/compiler/parser/parser.h"
#include "zomlang/compiler/source/manager.h"

using namespace zomlang::compiler;
using Clock = std::chrono::high_resolution_clock;

static const char* kSampleProgram = R"(
module example;

/// A sample class for benchmarking
class Counter {
  private mut value: i32 = 0;
  static let instances: i32 = 0;
  const maximum: i32 = 1000;

  init(initial: i32) {
    self.value = initial;
  }

  fun increment() -> unit {
    self.value = self.value + 1;
  }

  fun current() -> i32 {
    return self.value;
  }
}

interface Drawable {
  fun draw(this) -> unit;
}

export struct Point<T: numeric> {
  public mut x: T;
  public mut y: T;

  init(this, x: T, y: T) {
    this.x = x;
    this.y = y;
  }

  fun dot(this, other: Point<T>) -> T {
    return this.x * other.x + this.y * other.y;
  }
}

enum Message {
  Quit(i32),
  Move(i32),
  Write(str | i32),
  ChangeColor(u8, u8, u8) = 10,
}

alias UserId = u64;
const maximumRetries: i32 = 3;

fun fibonacci(n: u32) -> u64 {
  if (n <= 1) {
    return n;
  }
  return fibonacci(n - 1) + fibonacci(n - 2);
}

fun sum(values: [i32]) -> i32 {
  let total: i32 = 0;
  for (let value in values) {
    total = total + value;
  }
  return total;
}

fun handle(message: Message) -> unit {
  match (message) {
    when Message.Quit(code) => { }
    when Message.Move(value) => { }
    when Message.Write(text) => { }
    when Message.ChangeColor(red, green, blue) => { }
  }
}

fun main() {
  let counter = 0;
  while (counter < 10) {
    counter = counter + 1;
  }

  for (let index = 0; index < 10; index = index + 1) {
    counter = counter + index;
  }

  let values = [1, 2, 3, 4, 5];
  for (let value in values) {
    counter = counter + value;
  }
}
)";

struct BenchmarkResult {
  const char* name;
  double ms;
  size_t tokens;
  size_t lines;
};

static BenchmarkResult runLexerBenchmark(source::SourceManager& sourceMgr,
                                         basic::StringPool& stringPool,
                                         const source::BufferId& bufferId,
                                         const basic::LangOptions& opts) {
  auto start = Clock::now();
  size_t tokenCount = 0;

  for (int iter = 0; iter < 100; ++iter) {
    diagnostics::SourceDiagnosticDraftBuffer diagnosticFacts(sourceMgr, bufferId);
    lexer::Lexer sourceLexer(sourceMgr, diagnosticFacts.lexerEmitter(), opts, stringPool, bufferId);
    lexer::Token token;
    do {
      sourceLexer.lex(token);
      ++tokenCount;
    } while (!token.is(ast::SyntaxKind::EndOfFile));
  }

  auto end = Clock::now();
  double ms = std::chrono::duration<double, std::milli>(end - start).count() / 100.0;
  return {"lexer", ms, tokenCount / 100, 0};
}

static BenchmarkResult runParserBenchmark(source::SourceManager& sourceMgr,
                                          basic::StringPool& stringPool,
                                          const source::BufferId& bufferId,
                                          const basic::LangOptions& opts) {
  auto start = Clock::now();
  size_t successCount = 0;

  for (int iter = 0; iter < 50; ++iter) {
    diagnostics::SourceDiagnosticDraftBuffer diagnosticFacts(sourceMgr, bufferId);
    parser::Parser sourceParser(sourceMgr, diagnosticFacts, opts, stringPool, bufferId);
    if (sourceParser.parse() != zc::none) { ++successCount; }
  }

  auto end = Clock::now();
  double ms = std::chrono::duration<double, std::milli>(end - start).count() / 50.0;
  return {"parser", ms, 0, successCount};
}

int main(int argc, char* argv[]) {
  printf("=== ZOM Compiler Frontend Benchmark ===\n\n");

  basic::LangOptions opts;
  source::SourceManager sourceMgr;
  basic::StringPool stringPool;

  const zc::StringPtr source(kSampleProgram);
  const auto bufferId = sourceMgr.addMemBufferCopy(source.asBytes(), "benchmark.zom");

  // Count lines
  size_t lines = 1;
  for (const char* cursor = kSampleProgram; *cursor != '\0'; ++cursor) {
    if (*cursor == '\n') { ++lines; }
  }

  printf("Source: %zu lines, %zu chars\n\n", lines, source.size());

  auto lexResult = runLexerBenchmark(sourceMgr, stringPool, bufferId, opts);
  printf("[Lexer]  %.2f ms  (%zu tokens, %.0f tokens/ms)\n", lexResult.ms, lexResult.tokens,
         lexResult.ms > 0 ? lexResult.tokens / lexResult.ms : 0);

  auto parseResult = runParserBenchmark(sourceMgr, stringPool, bufferId, opts);
  printf("[Parser] %.2f ms  (%.0f%% success rate)\n", parseResult.ms,
         parseResult.lines > 0 ? parseResult.lines * 2.0 : 0);

  printf("\nDone.\n");
  return 0;
}
