// Copyright (c) 2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/checker/checker.h"

#include "zc/core/common.h"
#include "zc/core/string.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/statement.h"
#include "zomlang/compiler/basic/compiler-opts.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/parser/parser.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang {
namespace compiler {
namespace checker {

ZC_TEST("CheckerTest_BasicParsingWorks") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x: i32 = 42;").asBytes(), "test.zom");
  parser::Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);
  auto ast = parser.parse();

  ZC_EXPECT(ast != zc::none, "Parser should successfully parse valid code");
  // TODO: Add type checking once Checker implementation is available
}

ZC_TEST("CheckerTest_TypeMismatchError") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x: i32 = \"string\";").asBytes(), "test.zom");
  parser::Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);
  auto ast = parser.parse();

  ZC_EXPECT(ast != zc::none, "Parser should parse syntactically valid code");
  // TODO: Add type checking once Checker implementation is available
}

ZC_TEST("CheckerTest_UndefinedVariableError") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId =
      sourceManager->addMemBufferCopy(zc::str("let x: i32 = y + 1;").asBytes(), "test.zom");
  parser::Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);
  auto ast = parser.parse();

  ZC_EXPECT(ast != zc::none);
  // TODO: Add type checking once Checker implementation is available
}

ZC_TEST("CheckerTest_FunctionParameterTypeChecking") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;

  auto bufferId = sourceManager->addMemBufferCopy(
      zc::str("fun add(a: i32, b: str) -> i32 { return a + b; }\n").asBytes(), "test.zom");
  parser::Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);
  auto ast = parser.parse();

  ZC_EXPECT(ast != zc::none);
  // TODO: Add type checking once Checker implementation is available
}

}  // namespace checker
}  // namespace compiler
}  // namespace zomlang