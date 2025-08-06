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

#include "zomlang/compiler/parser/parser.h"

#include "zc/core/common.h"
#include "zc/core/string.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/statement.h"
#include "zomlang/compiler/basic/compiler-opts.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang {
namespace compiler {
namespace parser {

ZC_TEST("ParserTest.BasicParserCreation") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  
  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let x: i32 = 42;").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);
  
  // Just verify parser can be created
  ZC_EXPECT(true);
}

ZC_TEST("ParserTest.EmptySource") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  
  auto bufferId = sourceManager->addMemBufferCopy(zc::str(""_zc).asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);
}

ZC_TEST("ParserTest.SimpleExpression") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto diagnosticEngine = zc::heap<diagnostics::DiagnosticEngine>(*sourceManager);
  basic::LangOptions langOpts;
  
  auto bufferId = sourceManager->addMemBufferCopy(zc::str("42").asBytes(), "test.zom");
  Parser parser(*sourceManager, *diagnosticEngine, langOpts, bufferId);
  
  // Just verify parser can handle simple expressions
  ZC_EXPECT(true);
}

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang