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
#include <string>
#include <vector>

#include "zc/core/string.h"
#include "zomlang/compiler/basic/string-pool.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/diagnostics/diagnostic-consumer.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/lexer/lexer.h"
#include "zomlang/compiler/parser/parser.h"
#include "zomlang/compiler/source/manager.h"

using namespace zomlang::compiler;
using Clock = std::chrono::high_resolution_clock;

static const char* kSampleProgram = R"(
module example

import std.collections.{Vec, Map}
import std.io.println

/// A sample class for benchmarking
class Person {
  let name: str
  let age: u32
  mut score: f64 = 0.0

  init(name: str, age: u32) {
    this.name = name
    this.age = age
  }

  fun greet(): str {
    return "Hello, " + name + "!"
  }

  fun birthday() {
    age = age + 1
  }
}

interface Drawable {
  fun draw(): unit
}

struct Point {
  x: f64
  y: f64
}

enum Color {
  Red,
  Green,
  Blue,
  Custom(str),
}

alias UserId = u64

let people: Vec<Person> = Vec.new()
const MAX_RETRIES = 3
mut counter = 0

fun fibonacci(n: u32): u64 {
  if n <= 1 { return n }
  return fibonacci(n - 1) + fibonacci(n - 2)
}

fun process(items: Vec<str>): Vec<u32> {
  let results: Vec<u32> = Vec.new()
  for item in items {
    let parsed = item.parseU32()
    if parsed? {
      results.push(parsed!)
    }
  }
  return results
}

fun main() {
  let p = Person.new("Alice", 30)
  println(p.greet())

  for i in 0..10 {
    counter = counter + i
  }

  match Color.Red {
    Color.Red => println("red")
    Color.Green => println("green")
    Color.Blue => println("blue")
    Color.Custom(name) => println(name)
  }

  let nums = [1, 2, 3, 4, 5]
  let doubled = nums.map(fn(n) { n * 2 })
  println(doubled)
}
)";

struct BenchmarkResult {
  const char* name;
  double ms;
  size_t tokens;
  size_t lines;
};

static BenchmarkResult runLexerBenchmark(source::SourceManager& sourceMgr,
                                         diagnostics::DiagnosticEngine& diagEngine,
                                         basic::StringPool& stringPool,
                                         const source::BufferId& bufferId,
                                         const basic::LangOptions& opts) {
  auto start = Clock::now();
  size_t tokenCount = 0;

  for (int iter = 0; iter < 100; ++iter) {
    Lexer lexer(sourceMgr, diagEngine, opts, stringPool, bufferId);
    Token token;
    do {
      lexer.lex(token);
      ++tokenCount;
    } while (!token.is(ast::SyntaxKind::EndOfFile));
  }

  auto end = Clock::now();
  double ms = std::chrono::duration<double, std::milli>(end - start).count() / 100.0;
  return {"lexer", ms, tokenCount / 100, 0};
}

static BenchmarkResult runParserBenchmark(source::SourceManager& sourceMgr,
                                          diagnostics::DiagnosticEngine& diagEngine,
                                          basic::StringPool& stringPool,
                                          const source::BufferId& bufferId,
                                          const basic::LangOptions& opts) {
  auto start = Clock::now();
  size_t successCount = 0;

  for (int iter = 0; iter < 50; ++iter) {
    Parser parser(sourceMgr, diagEngine, opts, stringPool, bufferId);
    if (parser.parse()) { ++successCount; }
  }

  auto end = Clock::now();
  double ms = std::chrono::duration<double, std::milli>(end - start).count() / 50.0;
  return {"parser", ms, 0, successCount};
}

int main(int argc, char* argv[]) {
  printf("=== ZOM Compiler Frontend Benchmark ===\n\n");

  basic::LangOptions opts;
  source::SourceManager sourceMgr;
  diagnostics::DiagnosticEngine diagEngine(sourceMgr);
  basic::StringPool stringPool;

  zc::String source(kSampleProgram);
  auto bufferId = sourceMgr.addMemBuffer(zc::Str("benchmark.zom"), source.asStringPtr());

  // Count lines
  size_t lines = 1;
  for (char c : kSampleProgram) {
    if (c == '\n') ++lines;
  }

  printf("Source: %zu lines, %zu chars\n\n", lines, strlen(kSampleProgram));

  auto lexResult = runLexerBenchmark(sourceMgr, diagEngine, stringPool, bufferId, opts);
  printf("[Lexer]  %.2f ms  (%zu tokens, %.0f tokens/ms)\n", lexResult.ms, lexResult.tokens,
         lexResult.ms > 0 ? lexResult.tokens / lexResult.ms : 0);

  auto parseResult = runParserBenchmark(sourceMgr, diagEngine, stringPool, bufferId, opts);
  printf("[Parser] %.2f ms  (%.0f%% success rate)\n", parseResult.ms,
         parseResult.lines > 0 ? parseResult.lines * 2.0 : 0);

  printf("\nDone.\n");
  return 0;
}
