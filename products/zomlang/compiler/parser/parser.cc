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

#include "zomlang/compiler/diagnostics/diagnostic-fact-buffer.h"
#include "zomlang/compiler/parser/parser-impl.h"

namespace zomlang {
namespace compiler {
namespace parser {

Parser::Impl::Impl(const source::SourceManager& sourceMgr,
                   diagnostics::DiagnosticFactBuffer& diagnosticFacts,
                   const basic::LangOptions& langOpts, basic::StringPool& stringPool,
                   const source::BufferId& bufferId)
    : sourceMgr(sourceMgr),
      diagnosticFacts(diagnosticFacts),
      diagnosticEngine(diagnosticFacts.parserEmitter()),
      bufferId(bufferId),
      context(sourceMgr, diagnosticFacts, langOpts, stringPool, bufferId) {}

const lexer::Token& Parser::Impl::tokenAt(size_t index) const { return context.tokenAt(index); }

ast::SyntaxKind Parser::Impl::kindAt(size_t index) const { return context.kindAt(index); }

bool Parser::Impl::isAtEnd(size_t index) const {
  return kindAt(index) == ast::SyntaxKind::EndOfFile;
}

TokenCursor Parser::Impl::tokenCursorAt(size_t index) const { return context.cursorAt(index); }

source::SourceRange Parser::Impl::rangeFor(size_t start, size_t end) const {
  return context.rangeFor(start, end);
}

ast::IdentId Parser::Impl::internIdent(AstFactory& builder, size_t index) const {
  const lexer::Token& token = tokenAt(index);
  if (token.is(ast::SyntaxKind::EndOfFile)) { return ast::IdentId(); }
  zc::StringPtr text = token.getValue();
  if (text.size() == 0) { text = tokenLabel(token); }
  return builder.internIdent(text);
}

ast::StringId Parser::Impl::internString(AstFactory& builder, size_t index) const {
  const lexer::Token& token = tokenAt(index);
  if (token.is(ast::SyntaxKind::EndOfFile)) { return ast::StringId(); }
  zc::StringPtr text = token.getValue();
  if (text.size() == 0) { text = tokenLabel(token); }
  return builder.internString(text);
}

bool Parser::Impl::tokenTextEquals(size_t index, zc::StringPtr expected) const {
  const lexer::Token& token = tokenAt(index);
  if (token.is(ast::SyntaxKind::EndOfFile)) { return false; }
  zc::StringPtr text = token.getValue();
  if (text.size() == 0) { text = tokenLabel(token); }
  return text == expected;
}

bool Parser::Impl::isSoftKeyword(size_t index, zc::StringPtr expected) const {
  return kindAt(index) == ast::SyntaxKind::Identifier && tokenTextEquals(index, expected);
}

void Parser::Impl::addNodeIfPresent(zc::Vector<ast::NodeId>& nodes, ast::NodeId node) const {
  if (node) { nodes.add(node); }
}

ast::IdentList Parser::Impl::makeIdentList(AstFactory& builder, size_t start, size_t end) const {
  zc::Vector<ast::IdentId> segments;
  for (size_t index = start; index < end; ++index) {
    if (isAttributePathSegment(kindAt(index)) || kindAt(index) == ast::SyntaxKind::ThisKeyword) {
      segments.add(internIdent(builder, index));
    }
  }
  return builder.makeIdentList(segments.asPtr());
}

ast::NodeId Parser::Impl::makeStatementListItem(AstFactory& builder, ast::NodeId item,
                                                source::SourceRange range,
                                                ast::NodeId attrs) const {
  return builder.makeStatementListItem(zc::mv(range), item, attrs);
}

void Parser::Impl::emitUnexpected(const lexer::Token& where) const {
  diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(where.getLocation());
}

source::SourceLoc Parser::Impl::diagnosticLoc(size_t index) const {
  return context.diagnosticLoc(index);
}

ast::Tree Parser::Impl::buildTree() {
  AstFactory builder;
  ast::NodeId moduleNode;
  zc::Vector<ast::NodeId> statements;
  bool firstSourceElement = true;

  TokenCursor cursor = tokenCursorAt(0);
  while (!cursor.isAtEnd()) {
    const size_t index = cursor.position();
    const SourceElementParseResult elementResult = parseSourceElement(
        builder, cursor, static_cast<size_t>(-1), SourceElementContext::ModuleItem);
    const size_t end = elementResult.boundary.end;
    const size_t elementStart = elementResult.boundary.head;
    const ast::SyntaxKind first = kindAt(elementStart);
    const bool isModuleDeclaration = first == ast::SyntaxKind::ModuleKeyword ||
                                     (first == ast::SyntaxKind::ExportKeyword &&
                                      kindAt(elementStart + 1) == ast::SyntaxKind::ModuleKeyword);
    if (isModuleDeclaration && firstSourceElement && !moduleNode) {
      moduleNode = elementResult.node;
    } else {
      if (isModuleDeclaration) {
        diagnosticEngine.diagnose<diagnostics::DiagID::ModuleDeclarationMustBeFirst>(
            tokenAt(elementStart).getLocation());
      }
      if (elementResult.node) {
        statements.add(makeStatementListItem(builder, elementResult.node, rangeFor(index, end),
                                             elementResult.attrs));
      }
    }

    firstSourceElement = false;
    if (cursor.position() <= index) { cursor.moveTo(index + 1); }
  }

  const ast::NodeList statementList = builder.makeList(statements.asPtr());
  const ast::NodeId root = builder.makeSourceFile(rangeFor(0, cursor.position() + 1),
                                                  builder.internString(context.fileIdentifier()),
                                                  moduleNode, statementList);
  builder.setRoot(root);
  return builder.finish();
}

Parser::Parser(const source::SourceManager& sourceMgr,
               diagnostics::DiagnosticFactBuffer& diagnosticFacts,
               const basic::LangOptions& langOpts, basic::StringPool& stringPool,
               const source::BufferId& bufferId)
    : impl(zc::heap<Impl>(sourceMgr, diagnosticFacts, langOpts, stringPool, bufferId)) {}

Parser::~Parser() noexcept(false) = default;

zc::Maybe<ast::Tree> Parser::parse() {
  ZC_IREQUIRE(!impl->parseAttempted, "parser instances are single-use");
  impl->parseAttempted = true;
  impl->parseSucceeded = false;
  impl->tokenSnapshotTaken = false;
  trace::FunctionTracer functionTracer(trace::TraceCategory::kParser, __FUNCTION__);
  const size_t initialErrorCount = impl->context.errorCount();
  ast::Tree tree = impl->buildTree();
  impl->diagnoseTokenPatterns();
  ZC_IF_SOME(schemaFailure, ast::verifySchemaFailure(tree)) {
    impl->diagnosticFacts.reportInvariant(zc::mv(schemaFailure));
  }
  if (impl->diagnosticFacts.hasInvariantViolation() ||
      impl->context.errorCount() != initialErrorCount) {
    trace::traceEvent(trace::TraceCategory::kParser, "Parse failed");
    return zc::none;
  }
  trace::traceEvent(trace::TraceCategory::kParser, "Parse completed successfully");
  impl->parseSucceeded = true;
  return zc::mv(tree);
}

zc::Maybe<ParsedTokenSnapshot> Parser::takeTokenSnapshot() {
  if (!impl->parseSucceeded || impl->tokenSnapshotTaken) { return zc::none; }
  impl->tokenSnapshotTaken = true;
  return ParsedTokenSnapshot(impl->sourceMgr, impl->bufferId,
                             impl->context.copyBufferedTokenRanges());
}

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
