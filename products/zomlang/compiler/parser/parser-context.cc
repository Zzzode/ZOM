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

#include "zc/core/debug.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"

namespace zomlang {
namespace compiler {
namespace parser {

ParserContext::ParserContext(const source::SourceManager& sourceMgr,
                             diagnostics::DiagnosticEngine& diagnosticEngine,
                             const source::BufferId& bufferId)
    : sourceMgr(sourceMgr), diagnosticEngine(diagnosticEngine), bufferId(bufferId) {}

void ParserContext::resetTokens(zc::ArrayPtr<const lexer::Token> tokens) { cursor.reset(tokens); }

size_t ParserContext::tokenCount() const { return cursor.size(); }

size_t ParserContext::tokenCountWithoutEof() const {
  if (cursor.size() == 0) { return 0; }
  return cursor.size() - 1;
}

const lexer::Token& ParserContext::tokenAt(size_t index) const {
  ZC_IREQUIRE(index < cursor.size(), "parser token index outside token stream");
  return cursor.tokenAt(index);
}

ast::SyntaxKind ParserContext::kindAt(size_t index) const { return tokenAt(index).getKind(); }

source::SourceLoc ParserContext::diagnosticLoc(size_t index) const {
  if (index < cursor.size()) { return tokenAt(index).getLocation(); }
  ZC_IREQUIRE(cursor.size() != 0, "parser diagnostics require a token stream");
  return cursor.tokenAt(cursor.size() - 1).getLocation();
}

source::SourceRange ParserContext::rangeFor(size_t start, size_t end) const {
  if (cursor.size() == 0) {
    const source::SourceLoc loc = sourceMgr.getLocForBufferStart(bufferId);
    return source::SourceRange(loc, loc);
  }

  const size_t safeStart = start < cursor.size() ? start : cursor.size() - 1;
  const size_t safeEnd = end > start && end <= cursor.size() ? end - 1 : safeStart;
  return source::SourceRange(tokenAt(safeStart).getRange().getStart(),
                             tokenAt(safeEnd).getRange().getEnd());
}

zc::StringPtr ParserContext::fileIdentifier() const {
  return sourceMgr.getIdentifierForBuffer(bufferId);
}

diagnostics::DiagnosticEngine& ParserContext::diagnostics() const { return diagnosticEngine; }

const source::SourceManager& ParserContext::sourceManager() const { return sourceMgr; }

const source::BufferId& ParserContext::sourceBuffer() const { return bufferId; }

size_t ParserContext::errorCount() const { return diagnosticEngine.errorCount(); }

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
