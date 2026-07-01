// Copyright (c) 2024-2025 Zode.Z. All rights reserved
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
// See the License for the specific language governing permissions and
// limitations under the License.

#include "zomlang/compiler/parser/parser-context.h"

#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zc/ztest/test.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang {
namespace compiler {
namespace parser {

namespace {

lexer::Token tokenAt(source::SourceLoc base, ast::SyntaxKind kind, unsigned start, unsigned end,
                     zc::StringPtr text = ""_zc) {
  return lexer::Token(
      kind, source::SourceRange(base.getAdvancedLoc(start), base.getAdvancedLoc(end)), text);
}

zc::Vector<lexer::Token> makeTokenStream(source::SourceLoc base) {
  zc::Vector<lexer::Token> tokens;
  tokens.add(tokenAt(base, ast::SyntaxKind::LetKeyword, 0, 3, "let"_zc));
  tokens.add(tokenAt(base, ast::SyntaxKind::Identifier, 4, 9, "value"_zc));
  tokens.add(tokenAt(base, ast::SyntaxKind::Equals, 10, 11, "="_zc));
  tokens.add(tokenAt(base, ast::SyntaxKind::IntegerLiteral, 12, 13, "1"_zc));
  tokens.add(tokenAt(base, ast::SyntaxKind::Semicolon, 13, 14, ";"_zc));
  tokens.add(tokenAt(base, ast::SyntaxKind::EndOfFile, 14, 14));
  return tokens;
}

}  // namespace

ZC_TEST("ParserContextTest.TokenAccessAndFileIdentifier") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let value = 1;").asBytes(), "test.zom");
  diagnostics::DiagnosticEngine diagnosticEngine(*sourceManager);
  ParserContext context(*sourceManager, diagnosticEngine, bufferId);

  auto tokens = makeTokenStream(sourceManager->getLocForBufferStart(bufferId));
  context.resetTokens(tokens.asPtr());

  ZC_EXPECT(context.tokenCount() == 6);
  ZC_EXPECT(context.tokenCountWithoutEof() == 5);
  ZC_EXPECT(context.kindAt(1) == ast::SyntaxKind::Identifier);
  ZC_EXPECT(context.tokenAt(1).getValue() == "value");
  ZC_EXPECT(context.fileIdentifier() == "test.zom");
}

ZC_TEST("ParserContextTest.RangeAndDiagnosticLocationsClampToEof") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let value = 1;").asBytes(), "test.zom");
  diagnostics::DiagnosticEngine diagnosticEngine(*sourceManager);
  ParserContext context(*sourceManager, diagnosticEngine, bufferId);

  const source::SourceLoc base = sourceManager->getLocForBufferStart(bufferId);
  auto tokens = makeTokenStream(base);
  context.resetTokens(tokens.asPtr());

  const source::SourceRange range = context.rangeFor(1, 4);
  ZC_EXPECT(range.getStart() == base.getAdvancedLoc(4));
  ZC_EXPECT(range.getEnd() == base.getAdvancedLoc(13));
  ZC_EXPECT(range.getLength() == 9);

  ZC_EXPECT(context.diagnosticLoc(99) == base.getAdvancedLoc(14));
}

ZC_TEST("ParserContextTest.EmptyTokenRangeUsesBufferStart") {
  auto sourceManager = zc::heap<source::SourceManager>();
  auto bufferId = sourceManager->addMemBufferCopy(zc::str("let value = 1;").asBytes(), "test.zom");
  diagnostics::DiagnosticEngine diagnosticEngine(*sourceManager);
  ParserContext context(*sourceManager, diagnosticEngine, bufferId);

  const source::SourceLoc base = sourceManager->getLocForBufferStart(bufferId);
  const source::SourceRange range = context.rangeFor(0, 0);
  ZC_EXPECT(range.getStart() == base);
  ZC_EXPECT(range.getEnd() == base);
  ZC_EXPECT(context.errorCount() == 0);
}

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
