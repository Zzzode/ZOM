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

#include "zomlang/compiler/parser/parser.h"

#include "zomlang/compiler/parser/parser-impl.h"

namespace zomlang {
namespace compiler {
namespace parser {

Parser::Parser(const source::SourceManager& sourceMgr,
               diagnostics::DiagnosticEngine& diagnosticEngine, const basic::LangOptions& langOpts,
               basic::StringPool& stringPool, const source::BufferId& bufferId, ParseMode mode)
    : impl(zc::heap<Impl>(sourceMgr, diagnosticEngine, langOpts, stringPool, bufferId, mode)) {}

Parser::~Parser() noexcept(false) = default;

zc::Maybe<ast::Tree> Parser::parse() {
  trace::FunctionTracer functionTracer(trace::TraceCategory::kParser, __FUNCTION__);
  const size_t initialErrorCount = impl->context.errorCount();
  impl->lexAll();
  impl->diagnoseTokenPatterns();
  ast::Tree tree = impl->buildTree();
  const bool hadErrors = impl->context.errorCount() != initialErrorCount;
  ZC_IF_SOME(schemaFailure, ast::verifySchemaFailure(tree)) {
    impl->diagnosticEngine.diagnose<diagnostics::DiagID::ParserInvariantViolation>(
        impl->token.getLocation(), zc::mv(schemaFailure));
  }
  const bool hasErrors = impl->context.errorCount() != initialErrorCount;
  if (hasErrors && impl->parseMode == ParseMode::Strict) {
    trace::traceEvent(trace::TraceCategory::kParser, "Parse failed (strict mode)");
    return zc::none;
  }
  if (hadErrors) {
    trace::traceEvent(trace::TraceCategory::kParser, "Parse completed with errors (loose mode)");
  } else {
    trace::traceEvent(trace::TraceCategory::kParser, "Parse completed successfully");
  }
  return zc::mv(tree);
}

lexer::Token Parser::lookAhead(unsigned n) {
  lexer::LexerState state = impl->lexer.getCurrentState();
  lexer::Token saved = impl->token;
  lexer::Token result;
  for (unsigned i = 0; i < n; ++i) { impl->lexer.lex(result); }
  impl->lexer.restoreState(state);
  impl->token = zc::mv(saved);
  return result;
}

bool Parser::canLookAhead(unsigned n) { return !lookAhead(n).is(ast::SyntaxKind::EndOfFile); }

bool Parser::isLookAhead(unsigned n, ast::SyntaxKind kind) { return lookAhead(n).is(kind); }

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
