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

#include "zomlang/compiler/parser/parser.h"

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zc/core/string.h"
#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/factory.h"
#include "zomlang/compiler/ast/module.h"
#include "zomlang/compiler/ast/operator.h"
#include "zomlang/compiler/ast/statement.h"
#include "zomlang/compiler/ast/type.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/lexer/lexer.h"
#include "zomlang/compiler/lexer/token.h"
#include "zomlang/compiler/source/location.h"
#include "zomlang/compiler/source/manager.h"
#include "zomlang/compiler/trace/trace.h"

namespace zomlang {
namespace compiler {
namespace parser {

// ================================================================================
// Parser::Impl

struct Parser::Impl {
  Impl(const source::SourceManager& sourceMgr, diagnostics::DiagnosticEngine& diagnosticEngine,
       const basic::LangOptions& langOpts, const source::BufferId& bufferId) noexcept
      : bufferId(bufferId),
        sourceMgr(sourceMgr),
        diagnosticEngine(diagnosticEngine),
        lexer(sourceMgr, diagnosticEngine, langOpts, bufferId) {
    // Initialize the first token, which is kUnknown
    initializeToken();
  }
  ~Impl() noexcept(false) = default;

  ZC_DISALLOW_COPY_AND_MOVE(Impl);

  /// Helper to get the next token from the lexer
  void consumeToken() { lexer.lex(currentToken); }
  /// Helper to look at the current token
  const lexer::Token& peekToken() const { return currentToken; }
  /// Initialize currentToken by consuming the first token
  void initializeToken() { consumeToken(); }

  /// Lookahead functionality
  const lexer::Token& lookAheadToken(unsigned n) const {
    if (n == 0) return currentToken;
    return lexer.lookAhead(n);
  }

  bool canLookAheadToken(unsigned n) const {
    if (n == 0) return true;
    return lexer.canLookAhead(n);
  }

  bool isLookAheadToken(unsigned n, lexer::TokenKind kind) const {
    return lookAheadToken(n).is(kind);
  }

  const source::BufferId& bufferId;
  const source::SourceManager& sourceMgr;
  diagnostics::DiagnosticEngine& diagnosticEngine;
  lexer::Lexer lexer;
  lexer::Token currentToken;

  ParsingContext context;
};

// ================================================================================
// Parser

Parser::Parser(const source::SourceManager& sourceMgr,
               diagnostics::DiagnosticEngine& diagnosticEngine, const basic::LangOptions& langOpts,
               const source::BufferId& bufferId) noexcept
    : impl(zc::heap<Impl>(sourceMgr, diagnosticEngine, langOpts, bufferId)) {}

Parser::~Parser() noexcept(false) = default;

bool Parser::isListTerminator(ParsingContext context) const {
  const lexer::Token& currentToken = impl->peekToken();
  switch (context) {
    case ParsingContext::kSourceElements:
      return currentToken.is(lexer::TokenKind::kEOF);
    default:
      return false;
  }
}

bool Parser::isListElement(ParsingContext context, bool inErrorRecovery) const {
  const lexer::Token& currentToken = impl->peekToken();
  switch (context) {
    case ParsingContext::kSourceElements:
      return !currentToken.is(lexer::TokenKind::kSemicolon) && isStartOfStatement();
    default:
      return false;
  }
}

bool Parser::abortParsingListOrMoveToNextToken(ParsingContext context) {
  trace::traceEvent(trace::TraceCategory::kParser, "Error recovery", "Skipping token");

  // Simple error recovery: skip the current token and try again
  impl->consumeToken();
  return false;  // Continue parsing
}

zc::Maybe<zc::Own<ast::Node>> Parser::parse() {
  trace::FunctionTracer functionTracer(trace::TraceCategory::kParser, __FUNCTION__);

  impl->consumeToken();
  ZC_IF_SOME(sourceFileNode, parseSourceFile()) {
    trace::traceEvent(trace::TraceCategory::kParser, "Parse completed successfully");
    return zc::mv(sourceFileNode);
  }

  trace::traceEvent(trace::TraceCategory::kParser, "Parse failed");
  return zc::none;
}

zc::Maybe<zc::Own<ast::SourceFile>> Parser::parseSourceFile() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseSourceFile");

  // sourceFile: module;
  // module: moduleBody?;
  // moduleBody: moduleItemList;
  // moduleItemList: moduleItem+;

  source::SourceLoc startLoc = impl->peekToken().getLocation();

  zc::Vector<zc::Own<ast::Statement>> statements = parseList<ast::Statement>(
      ParsingContext::kSourceElements, ZC_BIND_METHOD(*this, parseModuleItem));

  trace::traceCounter(trace::TraceCategory::kParser, "Module items parsed"_zc,
                      zc::str(statements.size()));

  source::SourceLoc endLoc = impl->peekToken().getLocation();

  // Create the source file node
  zc::StringPtr fileName = impl->sourceMgr.getIdentifierForBuffer(impl->bufferId);
  zc::Own<ast::SourceFile> sourceFile =
      ast::factory::createSourceFile(zc::str(fileName), zc::mv(statements));
  sourceFile->setSourceRange(source::SourceRange(startLoc, endLoc));

  trace::traceEvent(trace::TraceCategory::kParser, "Source file created"_zc, fileName);
  return sourceFile;
}

zc::Maybe<zc::Own<ast::Statement>> Parser::parseModuleItem() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseModuleItem");

  // moduleItem:
  //   statementListItem
  //   | exportDeclaration
  //   | importDeclaration;

  const lexer::Token& currentToken = impl->peekToken();

  // Check for import declaration
  if (currentToken.is(lexer::TokenKind::kImportKeyword)) {
    trace::traceEvent(trace::TraceCategory::kParser, "Parsing import declaration");
    ZC_IF_SOME(importDecl, parseImportDeclaration()) { return zc::mv(importDecl); }
  }

  // Check for export declaration
  if (currentToken.is(lexer::TokenKind::kExportKeyword)) {
    trace::traceEvent(trace::TraceCategory::kParser, "Parsing export declaration");
    ZC_IF_SOME(exportDecl, parseExportDeclaration()) { return zc::mv(exportDecl); }
  }

  // Otherwise, parse as statement (statementListItem)
  trace::traceEvent(trace::TraceCategory::kParser, "Parsing statement");
  return parseStatement();
}

zc::Maybe<zc::Own<ast::ImportDeclaration>> Parser::parseImportDeclaration() {
  // importDeclaration: IMPORT modulePath ( AS identifierName )?;
  const lexer::Token& currentToken = impl->peekToken();

  // Expect IMPORT token
  if (!currentToken.is(lexer::TokenKind::kImportKeyword)) { return zc::none; }

  source::SourceLoc startLoc = currentToken.getLocation();
  impl->consumeToken();  // consume IMPORT

  // Parse modulePath
  ZC_IF_SOME(modulePath, parseModulePath()) {
    zc::Maybe<zc::String> alias = zc::none;

    // Check for optional AS clause
    const lexer::Token& nextToken = impl->peekToken();
    if (nextToken.is(lexer::TokenKind::kAsKeyword)) {
      impl->consumeToken();  // consume AS

      // Parse identifier name (alias)
      const lexer::Token& aliasToken = impl->peekToken();
      if (aliasToken.is(lexer::TokenKind::kIdentifier)) {
        alias = aliasToken.getText(impl->sourceMgr);
        impl->consumeToken();  // consume identifier
      } else {
        // Error: expected identifier after AS
        return zc::none;
      }
    }

    source::SourceLoc endLoc = impl->peekToken().getLocation();

    // Create ImportDeclaration with modulePath and optional alias
    auto importDecl = ast::factory::createImportDeclaration(zc::mv(modulePath), zc::mv(alias));
    importDecl->setSourceRange(source::SourceRange(startLoc, endLoc));
    return importDecl;
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::ModulePath>> Parser::parseModulePath() {
  // modulePath: bindingIdentifier ( PERIOD bindingIdentifier )*;
  const lexer::Token& currentToken = impl->peekToken();

  // Expect first bindingIdentifier
  if (!currentToken.is(lexer::TokenKind::kIdentifier)) { return zc::none; }

  source::SourceLoc startLoc = currentToken.getLocation();
  zc::Vector<zc::String> identifiers;
  identifiers.add(currentToken.getText(impl->sourceMgr));
  impl->consumeToken();  // consume first identifier

  // Parse optional additional identifiers separated by PERIOD
  while (true) {
    const lexer::Token& nextToken = impl->peekToken();
    if (nextToken.is(lexer::TokenKind::kPeriod)) {
      impl->consumeToken();  // consume PERIOD

      const lexer::Token& idToken = impl->peekToken();
      if (idToken.is(lexer::TokenKind::kIdentifier)) {
        identifiers.add(idToken.getText(impl->sourceMgr));
        impl->consumeToken();  // consume identifier
      } else {
        // Error: expected identifier after period
        return zc::none;
      }
    } else {
      break;  // No more periods, done parsing module path
    }
  }

  source::SourceLoc endLoc = impl->peekToken().getLocation();

  // Create ModulePath with collected identifiers
  auto modulePath = ast::factory::createModulePath(zc::mv(identifiers));
  modulePath->setSourceRange(source::SourceRange(startLoc, endLoc));
  return modulePath;
}

zc::Maybe<zc::Own<ast::ExportDeclaration>> Parser::parseExportDeclaration() {
  // exportDeclaration: EXPORT (exportModule | exportRename);
  // exportModule: bindingIdentifier;
  // exportRename: bindingIdentifier AS bindingIdentifier FROM modulePath;
  const lexer::Token& currentToken = impl->peekToken();

  // Expect EXPORT token
  if (!currentToken.is(lexer::TokenKind::kExportKeyword)) { return zc::none; }

  source::SourceLoc startLoc = currentToken.getLocation();
  impl->consumeToken();  // consume EXPORT

  const lexer::Token& nextToken = impl->peekToken();
  if (nextToken.is(lexer::TokenKind::kIdentifier)) {
    zc::String identifier = nextToken.getText(impl->sourceMgr);
    impl->consumeToken();  // consume identifier

    // Check if this is exportRename (identifier AS identifier FROM modulePath)
    const lexer::Token& followingToken = impl->peekToken();
    if (followingToken.is(lexer::TokenKind::kAsKeyword)) {
      impl->consumeToken();  // consume AS

      const lexer::Token& secondIdToken = impl->peekToken();
      if (secondIdToken.is(lexer::TokenKind::kIdentifier)) {
        zc::String alias = secondIdToken.getText(impl->sourceMgr);
        impl->consumeToken();  // consume second identifier

        const lexer::Token& fromToken = impl->peekToken();
        if (fromToken.is(lexer::TokenKind::kFromKeyword)) {
          impl->consumeToken();  // consume FROM

          // Parse modulePath
          ZC_IF_SOME(modulePath, parseModulePath()) {
            source::SourceLoc endLoc = impl->peekToken().getLocation();

            // Create ExportDeclaration with rename info
            auto exportDecl = ast::factory::createExportDeclaration(
                zc::mv(identifier), zc::mv(alias), zc::mv(modulePath));
            exportDecl->setSourceRange(source::SourceRange(startLoc, endLoc));
            return exportDecl;
          }
        }
      }
    } else {
      // Simple exportModule: just bindingIdentifier
      source::SourceLoc endLoc = impl->peekToken().getLocation();

      auto exportDecl = ast::factory::createExportDeclaration(zc::mv(identifier));
      exportDecl->setSourceRange(source::SourceRange(startLoc, endLoc));
      return exportDecl;
    }
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::Statement>> Parser::parseStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseStatement");

  const lexer::Token& currentToken = impl->peekToken();

  // Check for different statement types
  switch (currentToken.getKind()) {
    case lexer::TokenKind::kLeftBrace:
      return parseBlockStatement();
    case lexer::TokenKind::kSemicolon:
      return parseEmptyStatement();
    case lexer::TokenKind::kIfKeyword:
      return parseIfStatement();
    case lexer::TokenKind::kWhileKeyword:
      return parseWhileStatement();
    case lexer::TokenKind::kForKeyword:
      return parseForStatement();
    case lexer::TokenKind::kBreakKeyword:
      return parseBreakStatement();
    case lexer::TokenKind::kContinueKeyword:
      return parseContinueStatement();
    case lexer::TokenKind::kReturnKeyword:
      return parseReturnStatement();
    case lexer::TokenKind::kMatchKeyword:
      return parseMatchStatement();
    case lexer::TokenKind::kLetKeyword:
    case lexer::TokenKind::kConstKeyword:
      return parseVariableDeclaration();
    case lexer::TokenKind::kFunKeyword:
      return parseFunctionDeclaration();
    case lexer::TokenKind::kClassKeyword:
      return parseClassDeclaration();
    case lexer::TokenKind::kInterfaceKeyword:
      return parseInterfaceDeclaration();
    case lexer::TokenKind::kStructKeyword:
      return parseStructDeclaration();
    case lexer::TokenKind::kEnumKeyword:
      return parseEnumDeclaration();
    case lexer::TokenKind::kErrorKeyword:
      return parseErrorDeclaration();
    case lexer::TokenKind::kAliasKeyword:
      return parseAliasDeclaration();
    case lexer::TokenKind::kDebuggerKeyword:
      return parseDebuggerStatement();
    default:
      // Try to parse as expression statement
      return parseExpressionStatement();
  }
}

bool Parser::isStartOfStatement() const {
  const lexer::Token& currentToken = impl->peekToken();

  switch (currentToken.getKind()) {
    // Punctuation that can start statements
    case lexer::TokenKind::kAt:         // @decorator
    case lexer::TokenKind::kSemicolon:  // empty statement
    case lexer::TokenKind::kLeftBrace:  // block statement
    // Keywords that start statements
    case lexer::TokenKind::kLetKeyword:       // let declaration
    case lexer::TokenKind::kVarKeyword:       // var declaration
    case lexer::TokenKind::kFunKeyword:       // function declaration
    case lexer::TokenKind::kClassKeyword:     // class declaration
    case lexer::TokenKind::kBreakKeyword:     // break statement
    case lexer::TokenKind::kContinueKeyword:  // continue statement
    case lexer::TokenKind::kReturnKeyword:    // return statement
    case lexer::TokenKind::kThrowKeyword:     // throw statement
    case lexer::TokenKind::kTryKeyword:       // try statement
    case lexer::TokenKind::kMatchKeyword:     // match statement
    case lexer::TokenKind::kDebuggerKeyword:  // debugger statement
    case lexer::TokenKind::kDoKeyword:        // do statement
    case lexer::TokenKind::kWithKeyword:      // with statement
    case lexer::TokenKind::kSwitchKeyword:    // switch statement
      return true;

    // Keywords that might start statements depending on context
    case lexer::TokenKind::kImportKeyword:
      return isStartOfDeclaration();

    case lexer::TokenKind::kConstKeyword:
    case lexer::TokenKind::kExportKeyword:
      return isStartOfDeclaration();

    // Access modifiers and other contextual keywords
    case lexer::TokenKind::kAsyncKeyword:
    case lexer::TokenKind::kDeclareKeyword:
    case lexer::TokenKind::kInterfaceKeyword:
    case lexer::TokenKind::kModuleKeyword:
    case lexer::TokenKind::kNamespaceKeyword:
    case lexer::TokenKind::kGlobalKeyword:
      return true;

    case lexer::TokenKind::kAccessorKeyword:
    case lexer::TokenKind::kPublicKeyword:
    case lexer::TokenKind::kPrivateKeyword:
    case lexer::TokenKind::kProtectedKeyword:
    case lexer::TokenKind::kStaticKeyword:
    case lexer::TokenKind::kReadonlyKeyword:
    case lexer::TokenKind::kAbstractKeyword:
    case lexer::TokenKind::kOverrideKeyword:
      return isStartOfDeclaration();

    // Using keyword for using declarations
    case lexer::TokenKind::kUsingKeyword:
      return true;

    default:
      // Check if it's the start of an expression (which can be an expression statement)
      return isStartOfExpression();
  }
}

bool Parser::isStartOfLeftHandSideExpression() const {
  const lexer::Token& currentToken = impl->peekToken();

  switch (currentToken.getKind()) {
    // Keywords that can start left-hand side expressions
    case lexer::TokenKind::kThisKeyword:
    case lexer::TokenKind::kSuperKeyword:
    case lexer::TokenKind::kNewKeyword:
      return true;

    // Literals
    case lexer::TokenKind::kIntegerLiteral:
    case lexer::TokenKind::kFloatLiteral:
    case lexer::TokenKind::kStringLiteral:
      return true;

    // Grouping and collection literals
    case lexer::TokenKind::kLeftParen:    // Parenthesized expressions
    case lexer::TokenKind::kLeftBracket:  // Array literals
    case lexer::TokenKind::kLeftBrace:    // Object literals
      return true;

    // Function and class expressions
    case lexer::TokenKind::kFunKeyword:
    case lexer::TokenKind::kClassKeyword:
      return true;

    // Division operators (for regex literals)
    case lexer::TokenKind::kSlash:
    case lexer::TokenKind::kSlashEquals:
      return true;

    // Identifiers
    case lexer::TokenKind::kIdentifier:
      return true;

    // Import expressions (dynamic imports)
    case lexer::TokenKind::kImportKeyword:
      // TODO: Implement lookAhead(nextTokenIsOpenParenOrLessThanOrDot)
      return true;

    default:
      return false;
  }
}

bool Parser::isStartOfExpression() const {
  // First check if it's a left-hand side expression
  if (isStartOfLeftHandSideExpression()) { return true; }

  const lexer::Token& currentToken = impl->peekToken();

  switch (currentToken.getKind()) {
    // Unary operators
    case lexer::TokenKind::kPlus:
    case lexer::TokenKind::kMinus:
    case lexer::TokenKind::kTilde:
    case lexer::TokenKind::kExclamation:
    case lexer::TokenKind::kDeleteKeyword:
    case lexer::TokenKind::kTypeOfKeyword:
    case lexer::TokenKind::kVoidKeyword:
    case lexer::TokenKind::kPlusPlus:
    case lexer::TokenKind::kMinusMinus:
    case lexer::TokenKind::kLessThan:  // Type assertions
    case lexer::TokenKind::kAwaitKeyword:
    case lexer::TokenKind::kYieldKeyword:
    case lexer::TokenKind::kAt:  // Decorators
      return true;

    default:
      // Error tolerance: if we see the start of some binary operator,
      // we consider that the start of an expression
      // TODO: Implement isBinaryOperator() check
      return false;
  }
}

bool Parser::isStartOfDeclaration() const {
  const lexer::Token& currentToken = impl->peekToken();

  switch (currentToken.getKind()) {
    // Declaration keywords
    case lexer::TokenKind::kLetKeyword:
    case lexer::TokenKind::kVarKeyword:
    case lexer::TokenKind::kFunKeyword:
    case lexer::TokenKind::kClassKeyword:
    case lexer::TokenKind::kInterfaceKeyword:
    case lexer::TokenKind::kModuleKeyword:
    case lexer::TokenKind::kNamespaceKeyword:
    case lexer::TokenKind::kDeclareKeyword:
    case lexer::TokenKind::kGlobalKeyword:
      return true;

    // Access modifiers
    case lexer::TokenKind::kPublicKeyword:
    case lexer::TokenKind::kPrivateKeyword:
    case lexer::TokenKind::kProtectedKeyword:
    case lexer::TokenKind::kStaticKeyword:
    case lexer::TokenKind::kReadonlyKeyword:
    case lexer::TokenKind::kAccessorKeyword:
      return true;

    // Import/Export
    case lexer::TokenKind::kImportKeyword:
    case lexer::TokenKind::kExportKeyword:
      return true;

    // Async functions
    case lexer::TokenKind::kAsyncKeyword:
      return true;

    default:
      return false;
  }
}

// ================================================================================
// Utility methods

const lexer::Token& Parser::currentToken() const { return impl->peekToken(); }

void Parser::skipToken() { impl->consumeToken(); }

bool Parser::expectToken(lexer::TokenKind kind) {
  const lexer::Token& token = impl->peekToken();
  return token.is(kind);
}

bool Parser::consumeToken(lexer::TokenKind kind) {
  if (expectToken(kind)) {
    impl->consumeToken();
    return true;
  }
  return false;
}

zc::Maybe<zc::Vector<zc::Own<ast::Expression>>> Parser::parseArgumentList() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseArgumentList");

  // argumentList:
  //   (assignmentExpression | ELLIPSIS assignmentExpression) (
  //     COMMA (
  //       assignmentExpression
  //       | ELLIPSIS assignmentExpression
  //     )
  //   )*;

  if (!expectToken(lexer::TokenKind::kLeftParen)) { return zc::none; }

  impl->consumeToken();  // consume '('

  zc::Vector<zc::Own<ast::Expression>> arguments;

  if (!expectToken(lexer::TokenKind::kRightParen)) {
    do {
      ZC_IF_SOME(arg, parseAssignmentExpression()) { arguments.add(zc::mv(arg)); }
      else { return zc::none; }
    } while (consumeToken(lexer::TokenKind::kComma));
  }

  if (!consumeToken(lexer::TokenKind::kRightParen)) { return zc::none; }

  return zc::mv(arguments);
}

zc::Maybe<zc::Vector<zc::Own<ast::Type>>> Parser::parseTypeArgumentsInExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseTypeArgumentsInExpression");

  // typeArguments: LT typeArgumentList GT;
  // typeArgumentList: type (COMMA type)*;
  // This function parses type arguments in expression context, like f<number>(42)

  // Check if we have a '<' token that could start type arguments
  if (!expectToken(lexer::TokenKind::kLessThan)) { return zc::none; }

  impl->consumeToken();  // consume '<'

  zc::Vector<zc::Own<ast::Type>> typeArguments;

  if (!expectToken(lexer::TokenKind::kGreaterThan)) {
    do {
      ZC_IF_SOME(typeArg, parseType()) { typeArguments.add(zc::mv(typeArg)); }
      else { return zc::none; }
    } while (consumeToken(lexer::TokenKind::kComma));
  }

  if (!consumeToken(lexer::TokenKind::kGreaterThan)) { return zc::none; }

  // Check if the type argument list is followed by tokens that indicate
  // this should be treated as type arguments rather than comparison operators
  const lexer::Token& nextToken = impl->peekToken();
  if (nextToken.is(lexer::TokenKind::kLeftParen) ||      // f<T>()
      nextToken.is(lexer::TokenKind::kPeriod) ||         // f<T>.prop
      nextToken.is(lexer::TokenKind::kLeftBracket) ||    // f<T>[]
      nextToken.is(lexer::TokenKind::kStringLiteral)) {  // f<T>`template`
    return zc::mv(typeArguments);
  }

  // If not followed by appropriate tokens, this might be comparison operators
  return zc::none;
}

zc::Maybe<zc::Own<ast::Identifier>> Parser::parseIdentifier() {
  const lexer::Token& token = impl->peekToken();
  if (token.is(lexer::TokenKind::kIdentifier)) {
    zc::String identifier = token.getText(impl->sourceMgr);
    impl->consumeToken();
    return ast::factory::createIdentifier(zc::mv(identifier));
  }
  return zc::none;
}

zc::Maybe<zc::Own<ast::Identifier>> Parser::parseBindingIdentifier() { return parseIdentifier(); }

// ================================================================================
// Statement parsing implementations

zc::Maybe<zc::Own<ast::BlockStatement>> Parser::parseBlockStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseBlockStatement");

  source::SourceLoc startLoc = impl->peekToken().getLocation();
  if (!consumeToken(lexer::TokenKind::kLeftBrace)) { return zc::none; }

  zc::Vector<zc::Own<ast::Statement>> statements;

  while (!expectToken(lexer::TokenKind::kRightBrace) && !expectToken(lexer::TokenKind::kEOF)) {
    ZC_IF_SOME(stmt, parseStatement()) { statements.add(zc::mv(stmt)); }
    else {
      // Error recovery: skip token
      impl->consumeToken();
    }
  }

  source::SourceLoc endLoc = impl->peekToken().getLocation();
  if (!consumeToken(lexer::TokenKind::kRightBrace)) { return zc::none; }

  // Create block statement AST node
  auto blockStmt = ast::factory::createBlockStatement(zc::mv(statements));
  blockStmt->setSourceRange(source::SourceRange(startLoc, endLoc));
  return blockStmt;
}

zc::Maybe<zc::Own<ast::EmptyStatement>> Parser::parseEmptyStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseEmptyStatement");

  source::SourceLoc startLoc = impl->peekToken().getLocation();
  if (!consumeToken(lexer::TokenKind::kSemicolon)) { return zc::none; }
  source::SourceLoc endLoc = impl->peekToken().getLocation();

  // Create empty statement AST node
  auto emptyStmt = ast::factory::createEmptyStatement();
  emptyStmt->setSourceRange(source::SourceRange(startLoc, endLoc));
  return emptyStmt;
}

zc::Maybe<zc::Own<ast::ExpressionStatement>> Parser::parseExpressionStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseExpressionStatement");

  // expressionStatement: expression ";"
  //   where first token is not one of:
  //     "{" | "fun" | "class" | "let"
  //
  // This grammar rule means:
  // 1. An expression statement consists of an expression followed by semicolon
  // 2. The expression cannot start with:
  //    - A left brace (to avoid confusion with block statements)
  //    - The "fun" keyword (to avoid confusion with function declarations)
  //    - The "class" keyword (to avoid confusion with class declarations)
  //    - The "let" keyword (to avoid confusion with variable declarations)

  source::SourceLoc startLoc = impl->peekToken().getLocation();
  ZC_IF_SOME(expr, parseExpression()) {
    // Expect semicolon
    if (!consumeToken(lexer::TokenKind::kSemicolon)) { return zc::none; }
    source::SourceLoc endLoc = impl->peekToken().getLocation();

    // Create expression statement AST node
    auto exprStmt = ast::factory::createExpressionStatement(zc::mv(expr));
    exprStmt->setSourceRange(source::SourceRange(startLoc, endLoc));
    return exprStmt;
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::IfStatement>> Parser::parseIfStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseIfStatement");

  source::SourceLoc startLoc = impl->peekToken().getLocation();
  if (!consumeToken(lexer::TokenKind::kIfKeyword)) { return zc::none; }

  if (!consumeToken(lexer::TokenKind::kLeftParen)) { return zc::none; }

  ZC_IF_SOME(condition, parseExpression()) {
    if (!consumeToken(lexer::TokenKind::kRightParen)) { return zc::none; }

    ZC_IF_SOME(thenStmt, parseStatement()) {
      zc::Maybe<zc::Own<ast::Statement>> elseStmt = zc::none;

      if (expectToken(lexer::TokenKind::kElseKeyword)) {
        impl->consumeToken();
        elseStmt = parseStatement();
      }

      source::SourceLoc endLoc = impl->peekToken().getLocation();

      // Create if statement AST node
      auto ifStmt =
          ast::factory::createIfStatement(zc::mv(condition), zc::mv(thenStmt), zc::mv(elseStmt));
      ifStmt->setSourceRange(source::SourceRange(startLoc, endLoc));
      return ifStmt;
    }
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::WhileStatement>> Parser::parseWhileStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseWhileStatement");

  source::SourceLoc startLoc = impl->peekToken().getLocation();
  if (!consumeToken(lexer::TokenKind::kWhileKeyword)) { return zc::none; }

  if (!consumeToken(lexer::TokenKind::kLeftParen)) { return zc::none; }

  ZC_IF_SOME(condition, parseExpression()) {
    if (!consumeToken(lexer::TokenKind::kRightParen)) { return zc::none; }

    ZC_IF_SOME(body, parseStatement()) {
      source::SourceLoc endLoc = impl->peekToken().getLocation();

      // Create while statement AST node
      auto whileStmt = ast::factory::createWhileStatement(zc::mv(condition), zc::mv(body));
      whileStmt->setSourceRange(source::SourceRange(startLoc, endLoc));
      return whileStmt;
    }
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::ForStatement>> Parser::parseForStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseForStatement");

  if (!consumeToken(lexer::TokenKind::kForKeyword)) { return zc::none; }

  if (!consumeToken(lexer::TokenKind::kLeftParen)) { return zc::none; }

  // Parse init (optional)
  zc::Maybe<zc::Own<ast::Expression>> init = zc::none;
  if (!expectToken(lexer::TokenKind::kSemicolon)) { init = parseExpression(); }

  if (!consumeToken(lexer::TokenKind::kSemicolon)) { return zc::none; }

  // Parse condition (optional)
  zc::Maybe<zc::Own<ast::Expression>> condition = zc::none;
  if (!expectToken(lexer::TokenKind::kSemicolon)) { condition = parseExpression(); }

  if (!consumeToken(lexer::TokenKind::kSemicolon)) { return zc::none; }

  // Parse update (optional)
  zc::Maybe<zc::Own<ast::Expression>> update = zc::none;
  if (!expectToken(lexer::TokenKind::kRightParen)) { update = parseExpression(); }

  if (!consumeToken(lexer::TokenKind::kRightParen)) { return zc::none; }

  ZC_IF_SOME(body, parseStatement()) {
    (void)init;       // Suppress unused variable warning
    (void)condition;  // Suppress unused variable warning
    (void)update;     // Suppress unused variable warning
    (void)body;       // Suppress unused variable warning
    // TODO: Create for statement AST node
    return zc::none;
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::BreakStatement>> Parser::parseBreakStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseBreakStatement");

  if (!consumeToken(lexer::TokenKind::kBreakKeyword)) { return zc::none; }

  // Optional label
  zc::Maybe<zc::Own<ast::Identifier>> label = zc::none;
  if (expectToken(lexer::TokenKind::kIdentifier)) { label = parseIdentifier(); }

  if (!consumeToken(lexer::TokenKind::kSemicolon)) { return zc::none; }

  return ast::factory::createBreakStatement(zc::mv(label));
}

zc::Maybe<zc::Own<ast::ContinueStatement>> Parser::parseContinueStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseContinueStatement");

  if (!consumeToken(lexer::TokenKind::kContinueKeyword)) { return zc::none; }

  // Optional label
  zc::Maybe<zc::Own<ast::Identifier>> label = zc::none;
  if (expectToken(lexer::TokenKind::kIdentifier)) { label = parseIdentifier(); }

  if (!consumeToken(lexer::TokenKind::kSemicolon)) { return zc::none; }

  return ast::factory::createContinueStatement(zc::mv(label));
}

zc::Maybe<zc::Own<ast::ReturnStatement>> Parser::parseReturnStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseReturnStatement");

  source::SourceLoc startLoc = impl->peekToken().getLocation();
  if (!consumeToken(lexer::TokenKind::kReturnKeyword)) { return zc::none; }

  // Optional expression
  zc::Maybe<zc::Own<ast::Expression>> expr = zc::none;
  if (!expectToken(lexer::TokenKind::kSemicolon)) { expr = parseExpression(); }

  if (!consumeToken(lexer::TokenKind::kSemicolon)) { return zc::none; }
  source::SourceLoc endLoc = impl->peekToken().getLocation();

  // Create return statement AST node
  auto returnStmt = ast::factory::createReturnStatement(zc::mv(expr));
  returnStmt->setSourceRange(source::SourceRange(startLoc, endLoc));
  return returnStmt;
}

zc::Maybe<zc::Own<ast::MatchStatement>> Parser::parseMatchStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseMatchStatement");

  if (!consumeToken(lexer::TokenKind::kMatchKeyword)) { return zc::none; }

  if (!consumeToken(lexer::TokenKind::kLeftParen)) { return zc::none; }

  ZC_IF_SOME(expr, parseExpression()) {
    (void)expr;  // Suppress unused variable warning
    if (!consumeToken(lexer::TokenKind::kRightParen)) { return zc::none; }

    if (!consumeToken(lexer::TokenKind::kLeftBrace)) { return zc::none; }

    // TODO: Parse match clauses

    if (!consumeToken(lexer::TokenKind::kRightBrace)) { return zc::none; }

    // TODO: Create match statement AST node
    return zc::none;
  }

  return zc::none;
}

// ================================================================================
// Declaration parsing implementations

zc::Maybe<zc::Own<ast::Statement>> Parser::parseDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseDeclaration");

  const lexer::Token& token = impl->peekToken();

  switch (token.getKind()) {
    case lexer::TokenKind::kLetKeyword:
    case lexer::TokenKind::kConstKeyword:
      return parseVariableDeclaration();
    case lexer::TokenKind::kFunKeyword:
      return parseFunctionDeclaration();
    case lexer::TokenKind::kClassKeyword:
      return parseClassDeclaration();
    case lexer::TokenKind::kInterfaceKeyword:
      return parseInterfaceDeclaration();
    case lexer::TokenKind::kStructKeyword:
      return parseStructDeclaration();
    case lexer::TokenKind::kEnumKeyword:
      return parseEnumDeclaration();
    case lexer::TokenKind::kErrorKeyword:
      return parseErrorDeclaration();
    case lexer::TokenKind::kAliasKeyword:
      return parseAliasDeclaration();
    default:
      return zc::none;
  }
}

zc::Maybe<zc::Own<ast::VariableDeclaration>> Parser::parseVariableDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseVariableDeclaration");

  lexer::TokenKind declKind = impl->peekToken().getKind();
  if (declKind != lexer::TokenKind::kLetKeyword && declKind != lexer::TokenKind::kVarKeyword) {
    return zc::none;
  }

  source::SourceLoc startLoc = impl->peekToken().getLocation();
  impl->consumeToken();  // consume let

  ZC_IF_SOME(name, parseBindingIdentifier()) {
    // Optional type annotation
    zc::Maybe<zc::Own<ast::Type>> type = zc::none;
    if (expectToken(lexer::TokenKind::kColon)) {
      impl->consumeToken();
      type = parseType();
    }

    // Optional initializer
    zc::Maybe<zc::Own<ast::Expression>> initializer = zc::none;
    if (expectToken(lexer::TokenKind::kEquals)) {
      impl->consumeToken();
      initializer = parseAssignmentExpression();
    }

    source::SourceLoc endLoc = impl->peekToken().getLocation();

    // Create variable declaration AST node
    auto varDecl =
        ast::factory::createVariableDeclaration(zc::mv(name), zc::mv(type), zc::mv(initializer));
    varDecl->setSourceRange(source::SourceRange(startLoc, endLoc));
    return varDecl;
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::FunctionDeclaration>> Parser::parseFunctionDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseFunctionDeclaration");

  source::SourceLoc startLoc = impl->peekToken().getLocation();
  if (!consumeToken(lexer::TokenKind::kFunKeyword)) { return zc::none; }

  ZC_IF_SOME(name, parseBindingIdentifier()) {
    // Parse function signature (parameters and return type)
    if (!consumeToken(lexer::TokenKind::kLeftParen)) { return zc::none; }

    // TODO: Parse parameter list

    if (!consumeToken(lexer::TokenKind::kRightParen)) { return zc::none; }

    // Optional return type annotation
    if (expectToken(lexer::TokenKind::kColon)) {
      impl->consumeToken();
      // TODO: Parse return type
      parseType();
    }

    // Parse function body
    ZC_IF_SOME(body, parseBlockStatement()) {
      source::SourceLoc endLoc = impl->peekToken().getLocation();

      // Create function declaration AST node
      zc::Vector<zc::Own<ast::Statement>> bodyStatements;
      if (body->getKind() == ast::SyntaxKind::kBlockStatement) {
        // Extract statements from block
        const auto* blockStmt = static_cast<const ast::BlockStatement*>(body.get());
        for (const auto& stmt ZC_UNUSED : blockStmt->getStatements()) {
          // Note: This is a simplified approach. In practice, you might need to clone or move
          // statements For now, we'll create an empty body
        }
      }

      auto funcDecl = ast::factory::createFunctionDeclaration(zc::mv(name), zc::mv(bodyStatements));
      funcDecl->setSourceRange(source::SourceRange(startLoc, endLoc));
      return funcDecl;
    }
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::ClassDeclaration>> Parser::parseClassDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseClassDeclaration");

  source::SourceLoc startLoc = impl->peekToken().getLocation();
  if (!consumeToken(lexer::TokenKind::kClassKeyword)) { return zc::none; }

  ZC_IF_SOME(name, parseBindingIdentifier()) {
    // Optional extends clause
    if (expectToken(lexer::TokenKind::kExtendsKeyword)) {
      impl->consumeToken();
      // TODO: Parse superclass
      parseBindingIdentifier();
    }

    // Parse class body
    if (!consumeToken(lexer::TokenKind::kLeftBrace)) { return zc::none; }

    zc::Vector<zc::Own<ast::Statement>> bodyStatements;

    // Parse class members
    while (!expectToken(lexer::TokenKind::kRightBrace) && !expectToken(lexer::TokenKind::kEOF)) {
      ZC_IF_SOME(member, parseStatement()) { bodyStatements.add(zc::mv(member)); }
      else {
        // Skip invalid tokens
        impl->consumeToken();
      }
    }

    if (!consumeToken(lexer::TokenKind::kRightBrace)) { return zc::none; }

    source::SourceLoc endLoc = impl->peekToken().getLocation();

    // Create class declaration AST node
    auto classDecl = ast::factory::createClassDeclaration(zc::mv(name), zc::mv(bodyStatements));
    classDecl->setSourceRange(source::SourceRange(startLoc, endLoc));
    return classDecl;
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::InterfaceDeclaration>> Parser::parseInterfaceDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseInterfaceDeclaration");

  if (!consumeToken(lexer::TokenKind::kInterfaceKeyword)) { return zc::none; }

  ZC_IF_SOME(name, parseBindingIdentifier()) {
    (void)name;  // Suppress unused variable warning
    // TODO: Parse interface body
    // TODO: Create interface declaration AST node
    return zc::none;
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::StructDeclaration>> Parser::parseStructDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseStructDeclaration");

  if (!consumeToken(lexer::TokenKind::kStructKeyword)) { return zc::none; }

  ZC_IF_SOME(name, parseBindingIdentifier()) {
    (void)name;  // Suppress unused variable warning
    // TODO: Parse struct body
    // TODO: Create struct declaration AST node
    return zc::none;
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::EnumDeclaration>> Parser::parseEnumDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseEnumDeclaration");

  if (!consumeToken(lexer::TokenKind::kEnumKeyword)) { return zc::none; }

  ZC_IF_SOME(name, parseBindingIdentifier()) {
    (void)name;  // Suppress unused variable warning
    // TODO: Parse enum body
    // TODO: Create enum declaration AST node
    return zc::none;
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::ErrorDeclaration>> Parser::parseErrorDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseErrorDeclaration");

  if (!consumeToken(lexer::TokenKind::kErrorKeyword)) { return zc::none; }

  ZC_IF_SOME(name, parseBindingIdentifier()) {
    (void)name;  // Suppress unused variable warning
    // TODO: Parse error body
    // TODO: Create error declaration AST node
    return zc::none;
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::AliasDeclaration>> Parser::parseAliasDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseAliasDeclaration");

  source::SourceLoc startLoc = impl->peekToken().getLocation();
  if (!consumeToken(lexer::TokenKind::kAliasKeyword)) { return zc::none; }

  ZC_IF_SOME(name, parseBindingIdentifier()) {
    if (!consumeToken(lexer::TokenKind::kEquals)) { return zc::none; }

    ZC_IF_SOME(type, parseType()) {
      if (!consumeToken(lexer::TokenKind::kSemicolon)) { return zc::none; }
      source::SourceLoc endLoc = impl->peekToken().getLocation();

      auto typeAliasDecl = ast::factory::createAliasDeclaration(zc::mv(name), zc::mv(type));
      typeAliasDecl->setSourceRange(source::SourceRange(startLoc, endLoc));
      return typeAliasDecl;
    }
  }

  return zc::none;
}

// ================================================================================
// Expression parsing implementations

// Operator precedence levels
enum class OperatorPrecedence {
  kLowest = 0,
  kComma = 1,
  kAssignment = 2,
  kConditional = 3,
  kLogicalOr = 4,
  kLogicalAnd = 5,
  kBitwiseOr = 6,
  kBitwiseXor = 7,
  kBitwiseAnd = 8,
  kEquality = 9,
  kRelational = 10,
  kShift = 11,
  kAdditive = 12,
  kMultiplicative = 13,
  kExponentiation = 14,
  kUnary = 15,
  kUpdate = 16,
  kLeftHandSide = 17,
  kMember = 18,
  kPrimary = 19
};

namespace {

// Get binary operator precedence
OperatorPrecedence getBinaryOperatorPrecedence(lexer::TokenKind tokenKind) {
  switch (tokenKind) {
    case lexer::TokenKind::kComma:
      return OperatorPrecedence::kComma;
    case lexer::TokenKind::kBarBar:
      return OperatorPrecedence::kLogicalOr;
    case lexer::TokenKind::kAmpersandAmpersand:
      return OperatorPrecedence::kLogicalAnd;
    case lexer::TokenKind::kBar:
      return OperatorPrecedence::kBitwiseOr;
    case lexer::TokenKind::kCaret:
      return OperatorPrecedence::kBitwiseXor;
    case lexer::TokenKind::kAmpersand:
      return OperatorPrecedence::kBitwiseAnd;
    case lexer::TokenKind::kEqualsEquals:
    case lexer::TokenKind::kExclamationEquals:
      return OperatorPrecedence::kEquality;
    case lexer::TokenKind::kLessThan:
    case lexer::TokenKind::kGreaterThan:
    case lexer::TokenKind::kLessThanEquals:
    case lexer::TokenKind::kGreaterThanEquals:
      return OperatorPrecedence::kRelational;
    case lexer::TokenKind::kLessThanLessThan:
    case lexer::TokenKind::kGreaterThanGreaterThan:
      return OperatorPrecedence::kShift;
    case lexer::TokenKind::kPlus:
    case lexer::TokenKind::kMinus:
      return OperatorPrecedence::kAdditive;
    case lexer::TokenKind::kAsterisk:
    case lexer::TokenKind::kSlash:
    case lexer::TokenKind::kPercent:
      return OperatorPrecedence::kMultiplicative;
    case lexer::TokenKind::kAsteriskAsterisk:
      return OperatorPrecedence::kExponentiation;
    default:
      return OperatorPrecedence::kLowest;
  }
}

// Check if token is a binary operator
bool isBinaryOperator(lexer::TokenKind tokenKind) {
  return getBinaryOperatorPrecedence(tokenKind) > OperatorPrecedence::kLowest;
}

// Check if token is an assignment operator
bool isAssignmentOperator(lexer::TokenKind tokenKind) {
  switch (tokenKind) {
    case lexer::TokenKind::kEquals:                                   // =
    case lexer::TokenKind::kPlusEquals:                               // +=
    case lexer::TokenKind::kMinusEquals:                              // -=
    case lexer::TokenKind::kAsteriskEquals:                           // *=
    case lexer::TokenKind::kSlashEquals:                              // /=
    case lexer::TokenKind::kPercentEquals:                            // %=
    case lexer::TokenKind::kAsteriskAsteriskEquals:                   // **=
    case lexer::TokenKind::kLessThanLessThanEquals:                   // <<=
    case lexer::TokenKind::kGreaterThanGreaterThanEquals:             // >>=
    case lexer::TokenKind::kGreaterThanGreaterThanGreaterThanEquals:  // >>>=
    case lexer::TokenKind::kAmpersandEquals:                          // &=
    case lexer::TokenKind::kBarEquals:                                // |=
    case lexer::TokenKind::kCaretEquals:                              // ^=
    case lexer::TokenKind::kAmpersandAmpersandEquals:                 // &&=
    case lexer::TokenKind::kBarBarEquals:                             // ||=
    case lexer::TokenKind::kQuestionQuestionEquals:                   // ??=
      return true;
    default:
      return false;
  }
}

// Check if expression is a left-hand side expression
bool isLeftHandSideExpression(const ast::Expression& expr) {
  // Left-hand side expression checking
  // Valid left-hand side expressions include:
  // - Identifiers
  // - Member expressions (obj.prop)
  // - Parenthesized expressions (if inner is valid LHS)
  // - Array/object destructuring patterns

  switch (expr.getKind()) {
    case ast::SyntaxKind::kIdentifier:
    case ast::SyntaxKind::kMemberExpression:
    case ast::SyntaxKind::kLeftHandSideExpression:
      return true;

    case ast::SyntaxKind::kParenthesizedExpression:
      // For parenthesized expressions, we would need to check the inner expression
      // For now, simplified to return true
      return true;

    default:
      return false;
  }
}

}  // namespace

zc::Maybe<zc::Own<ast::Expression>> Parser::parseExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseExpression");

  // expression: assignmentExpression (COMMA assignmentExpression)*;
  // Parses a comma-separated list of assignment expressions

  ZC_IF_SOME(assignExpr, parseAssignmentExpressionOrHigher()) {
    zc::Own<ast::Expression> expr = zc::mv(assignExpr);
    // Handle comma operator
    while (expectToken(lexer::TokenKind::kComma)) {
      impl->consumeToken();
      ZC_IF_SOME(rightAssign, parseAssignmentExpressionOrHigher()) {
        zc::Own<ast::Expression> right = zc::mv(rightAssign);
        // Create comma expression AST node
        zc::Own<ast::BinaryOperator> op =
            ast::factory::createBinaryOperator(zc::str(","), ast::OperatorPrecedence::kLowest);
        zc::Own<ast::Expression> newExpr =
            ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(right));
        expr = zc::mv(newExpr);
      }
    }
    return zc::mv(expr);
  }

  return zc::none;
}

// Assignment expression parsing
zc::Maybe<zc::Own<ast::Expression>> Parser::parseAssignmentExpressionOrHigher() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser,
                                 "parseAssignmentExpressionOrHigher");

  // assignmentExpression:
  //   conditionalExpression
  //   | leftHandSideExpression ASSIGN assignmentExpression
  //   | leftHandSideExpression assignmentOperator assignmentExpression
  //   | leftHandSideExpression AND_ASSIGN assignmentExpression
  //   | leftHandSideExpression OR_ASSIGN assignmentExpression
  //   | leftHandSideExpression NULL_COALESCE_ASSIGN assignmentExpression;
  //
  // Parse binary expression first, then check for assignment

  // Parse binary expression with lowest precedence to get the left operand
  ZC_IF_SOME(expr, parseBinaryExpressionOrHigher()) {
    const lexer::Token& token = impl->peekToken();

    // Check for assignment operators - if found and expr is left-hand side, parse assignment
    if (isAssignmentOperator(token.getKind()) && isLeftHandSideExpression(*expr)) {
      zc::String opText = token.getText(impl->sourceMgr);
      impl->consumeToken();

      // Right-associative: recursively parse assignment expression
      ZC_IF_SOME(right, parseAssignmentExpressionOrHigher()) {
        auto op = ast::factory::createAssignmentOperator(zc::mv(opText));
        return ast::factory::createAssignmentExpression(zc::mv(expr), zc::mv(op), zc::mv(right));
      }
      return zc::none;
    }

    // Not an assignment, check for conditional expression (ternary operator)
    return parseConditionalExpressionRest(zc::mv(expr));
  }

  return zc::none;
}

// conditional expression rest parsing
zc::Maybe<zc::Own<ast::Expression>> Parser::parseConditionalExpressionRest(
    zc::Own<ast::Expression> leftOperand) {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseConditionalExpressionRest");

  // conditionalExpression:
  //   shortCircuitExpression (QUESTION assignmentExpression COLON assignmentExpression)?
  //
  // Check for ternary conditional operator
  if (!expectToken(lexer::TokenKind::kQuestion)) {
    // No conditional operator, return the left operand as-is
    return zc::mv(leftOperand);
  }

  impl->consumeToken();  // consume '?'

  // Parse the 'then' expression
  ZC_IF_SOME(thenExpr, parseAssignmentExpressionOrHigher()) {
    // Expect ':' token
    if (!consumeToken(lexer::TokenKind::kColon)) { return zc::none; }

    // Parse the 'else' expression
    ZC_IF_SOME(elseExpr, parseAssignmentExpressionOrHigher()) {
      // Create conditional expression AST node
      return ast::factory::createConditionalExpression(zc::mv(leftOperand), zc::mv(thenExpr),
                                                       zc::mv(elseExpr));
    }
  }

  return zc::none;
}

// Binary expression parsing
zc::Maybe<zc::Own<ast::Expression>> Parser::parseBinaryExpressionOrHigher() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseBinaryExpressionOrHigher");

  // Handles all binary expressions with precedence:
  //   bitwiseORExpression | bitwiseXORExpression | bitwiseANDExpression
  //   | equalityExpression | relationalExpression | shiftExpression
  //   | additiveExpression | multiplicativeExpression | exponentiationExpression
  // Uses operator precedence parsing for left-to-right associativity

  // Parse the left operand (unary expression or higher)
  ZC_IF_SOME(leftOperand, parseUnaryExpressionOrHigher()) {
    // Parse the rest of the binary expression with lowest precedence
    return parseBinaryExpressionRest(zc::mv(leftOperand));
  }

  return zc::none;
}

// Binary expression rest parsing
zc::Maybe<zc::Own<ast::Expression>> Parser::parseBinaryExpressionRest(
    zc::Own<ast::Expression> leftOperand) {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseBinaryExpressionRest");

  zc::Own<ast::Expression> expr = zc::mv(leftOperand);

  while (true) {
    const lexer::Token& token = impl->peekToken();

    if (!isBinaryOperator(token.getKind())) { break; }

    OperatorPrecedence currentPrecedence = getBinaryOperatorPrecedence(token.getKind());

    // For right-associative operators (like **), we need special handling
    bool isRightAssociative = (token.getKind() == lexer::TokenKind::kAsteriskAsterisk);

    zc::String opText = token.getText(impl->sourceMgr);
    impl->consumeToken();

    // Parse right operand with appropriate precedence
    OperatorPrecedence rightPrecedence =
        isRightAssociative
            ? static_cast<OperatorPrecedence>(static_cast<int>(currentPrecedence) - 1)
            : static_cast<OperatorPrecedence>(static_cast<int>(currentPrecedence) + 1);

    ZC_IF_SOME(rightOperand, parseUnaryExpressionOrHigher()) {
      ZC_IF_SOME(rightExpr,
                 parseBinaryExpressionRestWithPrecedence(zc::mv(rightOperand), rightPrecedence)) {
        // Create binary expression AST node
        auto op = ast::factory::createBinaryOperator(
            zc::mv(opText), static_cast<ast::OperatorPrecedence>(currentPrecedence));
        auto newExpr =
            ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(rightExpr));
        expr = zc::mv(newExpr);
      }
      else { return zc::none; }
    }
    else { return zc::none; }
  }

  return zc::mv(expr);
}

// Helper method for precedence-aware binary expression parsing
zc::Maybe<zc::Own<ast::Expression>> Parser::parseBinaryExpressionRestWithPrecedence(
    zc::Own<ast::Expression> leftOperand, OperatorPrecedence minPrecedence) {
  zc::Own<ast::Expression> expr = zc::mv(leftOperand);

  while (true) {
    const lexer::Token& token = impl->peekToken();

    if (!isBinaryOperator(token.getKind())) { break; }

    OperatorPrecedence currentPrecedence = getBinaryOperatorPrecedence(token.getKind());

    if (currentPrecedence < minPrecedence) { break; }

    bool isRightAssociative = (token.getKind() == lexer::TokenKind::kAsteriskAsterisk);

    zc::String opText = token.getText(impl->sourceMgr);
    impl->consumeToken();

    OperatorPrecedence rightPrecedence =
        isRightAssociative
            ? currentPrecedence
            : static_cast<OperatorPrecedence>(static_cast<int>(currentPrecedence) + 1);

    ZC_IF_SOME(rightOperand, parseUnaryExpressionOrHigher()) {
      ZC_IF_SOME(rightExpr,
                 parseBinaryExpressionRestWithPrecedence(zc::mv(rightOperand), rightPrecedence)) {
        auto op = ast::factory::createBinaryOperator(
            zc::mv(opText), static_cast<ast::OperatorPrecedence>(currentPrecedence));
        expr = ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(rightExpr));
      }
      else { return zc::none; }
    }
    else { return zc::none; }
  }

  return zc::mv(expr);
}

// Unary expression parsing
zc::Maybe<zc::Own<ast::Expression>> Parser::parseUnaryExpressionOrHigher() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseUnaryExpressionOrHigher");

  // unaryExpression:
  //   updateExpression
  //   | PLUS unaryExpression
  //   | MINUS unaryExpression
  //   | TILDE unaryExpression
  //   | EXCLAMATION unaryExpression
  //   | VOID unaryExpression
  //   | TYPEOF unaryExpression
  //   | AWAIT unaryExpression;
  //
  // Handles prefix unary operators and update expressions

  const lexer::Token& token = impl->peekToken();

  // Check for update expressions (++, --) first
  if (token.is(lexer::TokenKind::kPlusPlus) || token.is(lexer::TokenKind::kMinusMinus)) {
    ZC_IF_SOME(updateExpr, parseUpdateExpression()) {
      // Check for exponentiation operator after update expression
      if (expectToken(lexer::TokenKind::kAsteriskAsterisk)) {
        // Error: unary expression cannot be left operand of exponentiation
        // For now, we'll just return the update expression
        return zc::mv(updateExpr);
      }
      return zc::mv(updateExpr);
    }
  }

  // Check for unary operators
  if (token.is(lexer::TokenKind::kPlus) || token.is(lexer::TokenKind::kMinus) ||
      token.is(lexer::TokenKind::kTilde) || token.is(lexer::TokenKind::kExclamation) ||
      token.is(lexer::TokenKind::kVoidKeyword) || token.is(lexer::TokenKind::kTypeOfKeyword) ||
      token.is(lexer::TokenKind::kAwaitKeyword)) {
    return parseSimpleUnaryExpression();
  }

  // Otherwise, parse update expression
  ZC_IF_SOME(updateExpr, parseUpdateExpression()) { return zc::mv(updateExpr); }

  return zc::none;
}

// Simple unary expression parsing
zc::Maybe<zc::Own<ast::UnaryExpression>> Parser::parseSimpleUnaryExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseSimpleUnaryExpression");

  const lexer::Token& token = impl->peekToken();

  switch (token.getKind()) {
    case lexer::TokenKind::kPlus:
    case lexer::TokenKind::kMinus:
    case lexer::TokenKind::kTilde:
    case lexer::TokenKind::kExclamation: {
      return parsePrefixUnaryExpression();
    }
    case lexer::TokenKind::kVoidKeyword: {
      return parseVoidExpression();
    }
    case lexer::TokenKind::kTypeOfKeyword: {
      return parseTypeOfExpression();
    }
    default:
      // Parse update expression for other cases
      return parseUpdateExpression();
  }
}

// Prefix unary expression parsing
zc::Maybe<zc::Own<ast::UnaryExpression>> Parser::parsePrefixUnaryExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parsePrefixUnaryExpression");

  // prefixUnaryExpression:
  //   PLUS unaryExpression
  //   | MINUS unaryExpression
  //   | TILDE unaryExpression
  //   | EXCLAMATION unaryExpression;
  //
  // Parse prefix unary operators: +, -, ~, !

  const lexer::Token& token = impl->peekToken();
  zc::String operatorText = token.getText(impl->sourceMgr);
  impl->consumeToken();

  // Parse the operand (recursive call to parseSimpleUnaryExpression)
  ZC_IF_SOME(operand, parseSimpleUnaryExpression()) {
    // Create appropriate unary operator based on token kind
    zc::Own<ast::UnaryOperator> op;
    switch (token.getKind()) {
      case lexer::TokenKind::kPlus:
        op = ast::factory::createUnaryPlusOperator();
        break;
      case lexer::TokenKind::kMinus:
        op = ast::factory::createUnaryMinusOperator();
        break;
      case lexer::TokenKind::kExclamation:
        op = ast::factory::createLogicalNotOperator();
        break;
      case lexer::TokenKind::kTilde:
        op = ast::factory::createBitwiseNotOperator();
        break;
      default:
        // Fallback to generic operator
        op = ast::factory::createUnaryOperator(zc::mv(operatorText), true /* prefix */);
        break;
    }

    // Create prefix unary expression
    return ast::factory::createPrefixUnaryExpression(zc::mv(op), zc::mv(operand));
  }

  return zc::none;
}

// Void expression parsing
zc::Maybe<zc::Own<ast::VoidExpression>> Parser::parseVoidExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseVoidExpression");

  // voidExpression:
  //   VOID unaryExpression;
  //
  // Parse void operator expression

  const lexer::Token& token = impl->peekToken();
  zc::String operatorText = token.getText(impl->sourceMgr);
  impl->consumeToken();

  // Parse the operand
  ZC_IF_SOME(operand, parseSimpleUnaryExpression()) {
    // Create void expression
    return ast::factory::createVoidExpression(zc::mv(operand));
  }

  return zc::none;
}

// TypeOf expression parsing
zc::Maybe<zc::Own<ast::TypeOfExpression>> Parser::parseTypeOfExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseTypeOfExpression");

  // typeOfExpression:
  //   TYPEOF unaryExpression;
  //
  // Parse typeof operator expression

  const lexer::Token& token = impl->peekToken();
  zc::String operatorText = token.getText(impl->sourceMgr);
  impl->consumeToken();

  // Parse the operand
  ZC_IF_SOME(operand, parseSimpleUnaryExpression()) {
    // Create typeof expression
    return ast::factory::createTypeOfExpression(zc::mv(operand));
  }

  return zc::none;
}

// Left-hand side expression parsing
zc::Maybe<zc::Own<ast::LeftHandSideExpression>> Parser::parseLeftHandSideExpressionOrHigher() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser,
                                 "parseLeftHandSideExpressionOrHigher");

  // leftHandSideExpression:
  //   newExpression
  //   | callExpression
  //   | optionalExpression;

  const lexer::Token& token = impl->peekToken();

  zc::Maybe<zc::Own<ast::MemberExpression>> expression = zc::none;

  // Handle super keyword
  if (token.is(lexer::TokenKind::kSuperKeyword)) {
    ZC_IF_SOME(superExpr, parseSuperExpression()) { expression = zc::mv(superExpr); }
  }
  // Parse regular member expression
  else {
    ZC_IF_SOME(memberExpr, parseMemberExpressionOrHigher()) { expression = zc::mv(memberExpr); }
  }

  // If we have an expression, parse call expression rest
  ZC_IF_SOME(expr, expression) { return parseCallExpressionRest(zc::mv(expr)); }

  return zc::none;
}

// Helper method to parse member expression or higher
zc::Maybe<zc::Own<ast::MemberExpression>> Parser::parseMemberExpressionOrHigher() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseMemberExpressionOrHigher");

  // memberExpression:
  //   (primaryExpression | superProperty | NEW memberExpression arguments)
  //   (LBRACK expression RBRACK | PERIOD identifier)*;
  //
  // Based on TypeScript implementation, we parse primary expression first,
  // then handle member access chains

  zc::Maybe<zc::Own<ast::PrimaryExpression>> expression = zc::none;

  // Handle 'new' expressions
  if (impl->peekToken().is(lexer::TokenKind::kNewKeyword)) {
    ZC_IF_SOME(newExpr, parseNewExpression()) { expression = zc::mv(newExpr); }
  }
  // Parse primary expression
  else {
    ZC_IF_SOME(primaryExpr, parsePrimaryExpression()) { expression = zc::mv(primaryExpr); }
  }

  // Parse member expression rest (property access chains)
  ZC_IF_SOME(expr, expression) {
    return parseMemberExpressionRest(zc::mv(expr), true /* allowOptionalChain */);
  }

  return zc::none;
}

// Helper method to parse call expression rest
zc::Maybe<zc::Own<ast::LeftHandSideExpression>> Parser::parseCallExpressionRest(
    zc::Own<ast::MemberExpression> expression) {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseCallExpressionRest");

  // callExpression:
  //   (memberExpression arguments | superCall)
  //   (arguments | LBRACK expression RBRACK | PERIOD identifier)*;
  //
  // This method handles the iterative parsing of call chains

  zc::Own<ast::LeftHandSideExpression> result = zc::mv(expression);

  while (true) {
    const lexer::Token& token = impl->peekToken();

    // Handle function calls
    if (token.is(lexer::TokenKind::kLeftParen)) {
      // Parse argument list
      ZC_IF_SOME(arguments, parseArgumentList()) {
        // Create call expression
        result = ast::factory::createCallExpression(zc::mv(result), zc::mv(arguments));
        continue;
      }
      else { return zc::none; }
    }
    // Handle member access (.property)
    else if (token.is(lexer::TokenKind::kPeriod)) {
      impl->consumeToken();  // consume '.'
      ZC_IF_SOME(name, parseIdentifier()) {
        result = ast::factory::createPropertyAccessExpression(zc::mv(result), zc::mv(name), false);
        continue;
      }
    }
    // Handle computed member access ([expression])
    else if (token.is(lexer::TokenKind::kLeftBracket)) {
      impl->consumeToken();  // consume '['
      ZC_IF_SOME(index, parseExpression()) {
        if (!consumeToken(lexer::TokenKind::kRightBracket)) { return zc::none; }
        result = ast::factory::createElementAccessExpression(zc::mv(result), zc::mv(index), false);
        continue;
      }
    }
    // Handle optional chaining (?.) - simplified for now
    else if (token.is(lexer::TokenKind::kQuestion)) {
      impl->consumeToken();  // consume '?'
      const lexer::Token& nextToken = impl->peekToken();
      if (nextToken.is(lexer::TokenKind::kPeriod)) {
        impl->consumeToken();  // consume '.'

        ZC_IF_SOME(property, parseIdentifier()) {
          // Create optional expression directly
          result = ast::factory::createOptionalExpression(zc::mv(result), zc::mv(property));
          continue;
        }
      } else {
        // Not optional chaining, put back the '?' token by not consuming it
        // This is a limitation - we need to handle this case differently
        break;
      }
    } else {
      // No more call/member expressions
      break;
    }
  }

  return result;
}

zc::Maybe<zc::Own<ast::AssignmentExpression>> Parser::parseAssignmentExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseAssignmentExpression");

  // assignmentExpression:
  //   conditionalExpression
  //   | leftHandSideExpression assignmentOperator assignmentExpression;
  //
  // Only return AssignmentExpression if we actually have an assignment

  ZC_IF_SOME(expr, parseConditionalExpression()) {
    const lexer::Token& token = impl->peekToken();

    if (isAssignmentOperator(token.getKind()) && isLeftHandSideExpression(*expr)) {
      zc::String opText = token.getText(impl->sourceMgr);
      impl->consumeToken();

      ZC_IF_SOME(right, parseAssignmentExpressionOrHigher()) {
        auto op = ast::factory::createAssignmentOperator(zc::mv(opText));
        return ast::factory::createAssignmentExpression(zc::mv(expr), zc::mv(op), zc::mv(right));
      }
    }
  }

  // No assignment found, return none
  return zc::none;
}

zc::Maybe<zc::Own<ast::Expression>> Parser::parseShortCircuitExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseShortCircuitExpression");

  // shortCircuitExpression:
  //   logicalORExpression
  //   | coalesceExpression;
  //
  // Handles short-circuit evaluation for logical and null coalescing operators

  // Try to parse as logicalORExpression first
  ZC_IF_SOME(logicalExpr, parseLogicalOrExpression()) { return zc::mv(logicalExpr); }

  // If not a logicalORExpression, try coalesceExpression
  ZC_IF_SOME(coalesceExpr, parseCoalesceExpression()) { return zc::mv(coalesceExpr); }

  return zc::none;
}

zc::Maybe<zc::Own<ast::ConditionalExpression>> Parser::parseConditionalExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseConditionalExpression");

  // conditionalExpression:
  //   shortCircuitExpression (QUESTION assignmentExpression COLON assignmentExpression)?;
  //
  // Traditional parsing approach

  ZC_IF_SOME(expr, parseShortCircuitExpression()) {
    if (expectToken(lexer::TokenKind::kQuestion)) {
      impl->consumeToken();

      ZC_IF_SOME(thenExpr, parseAssignmentExpressionOrHigher()) {
        if (!consumeToken(lexer::TokenKind::kColon)) { return zc::none; }

        ZC_IF_SOME(elseExpr, parseAssignmentExpressionOrHigher()) {
          return ast::factory::createConditionalExpression(zc::mv(expr), zc::mv(thenExpr),
                                                           zc::mv(elseExpr));
        }
      }
    }

    // Not a conditional expression, return none since this method expects ConditionalExpression
    return zc::none;
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::Expression>> Parser::parseLogicalOrExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseLogicalOrExpression");

  // logicalORExpression:
  //   logicalANDExpression (OR logicalANDExpression)*;
  //
  // Handles logical OR operations with left-to-right associativity

  ZC_IF_SOME(expr, parseLogicalAndExpression()) {
    while (expectToken(lexer::TokenKind::kBarBar)) {
      zc::String opText = impl->peekToken().getText(impl->sourceMgr);
      impl->consumeToken();

      ZC_IF_SOME(right, parseLogicalAndExpression()) {
        // Create binary expression AST node
        auto op =
            ast::factory::createBinaryOperator(zc::mv(opText), ast::OperatorPrecedence::kLogicalOr);
        auto newExpr =
            ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(right));
        expr = zc::mv(newExpr);
      }
    }
    return zc::mv(expr);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::Expression>> Parser::parseLogicalAndExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseLogicalAndExpression");

  // logicalANDExpression:
  //   bitwiseORExpression (AND bitwiseORExpression)*;
  //
  // Handles logical AND operations with left-to-right associativity

  ZC_IF_SOME(expr, parseBitwiseOrExpression()) {
    while (expectToken(lexer::TokenKind::kAmpersandAmpersand)) {
      zc::String opText = impl->peekToken().getText(impl->sourceMgr);
      impl->consumeToken();

      ZC_IF_SOME(right, parseBitwiseOrExpression()) {
        // Create binary expression AST node
        auto op = ast::factory::createBinaryOperator(zc::mv(opText),
                                                     ast::OperatorPrecedence::kLogicalAnd);
        auto newExpr =
            ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(right));
        expr = zc::mv(newExpr);
      }
    }
    return zc::mv(expr);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::Expression>> Parser::parseBitwiseOrExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseBitwiseOrExpression");

  // bitwiseORExpression:
  //   bitwiseXORExpression (BITWISE_OR bitwiseXORExpression)*;
  //
  // Handles bitwise OR operations with left-to-right associativity

  ZC_IF_SOME(expr, parseBitwiseXorExpression()) {
    while (expectToken(lexer::TokenKind::kBar)) {
      zc::String opText = impl->peekToken().getText(impl->sourceMgr);
      impl->consumeToken();

      ZC_IF_SOME(right, parseBitwiseXorExpression()) {
        // Create binary expression AST node
        auto op =
            ast::factory::createBinaryOperator(zc::mv(opText), ast::OperatorPrecedence::kBitwiseOr);
        auto newExpr =
            ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(right));
        expr = zc::mv(newExpr);
      }
    }
    return zc::mv(expr);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::Expression>> Parser::parseBitwiseXorExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseBitwiseXorExpression");

  // bitwiseXORExpression:
  //   bitwiseANDExpression (BITWISE_XOR bitwiseANDExpression)*;
  //
  // Handles bitwise XOR operations with left-to-right associativity

  ZC_IF_SOME(expr, parseBitwiseAndExpression()) {
    while (expectToken(lexer::TokenKind::kCaret)) {
      zc::String opText = impl->peekToken().getText(impl->sourceMgr);
      impl->consumeToken();

      ZC_IF_SOME(right, parseBitwiseAndExpression()) {
        // Create binary expression AST node
        auto op = ast::factory::createBinaryOperator(zc::mv(opText),
                                                     ast::OperatorPrecedence::kBitwiseXor);
        auto newExpr =
            ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(right));
        expr = zc::mv(newExpr);
      }
    }
    return zc::mv(expr);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::Expression>> Parser::parseBitwiseAndExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseBitwiseAndExpression");

  // bitwiseANDExpression:
  //   equalityExpression (BITWISE_AND equalityExpression)*;
  //
  // Handles bitwise AND operations with left-to-right associativity

  ZC_IF_SOME(expr, parseEqualityExpression()) {
    while (expectToken(lexer::TokenKind::kAmpersand)) {
      zc::String opText = impl->peekToken().getText(impl->sourceMgr);
      impl->consumeToken();

      ZC_IF_SOME(right, parseEqualityExpression()) {
        // Create binary expression AST node
        auto op = ast::factory::createBinaryOperator(zc::mv(opText),
                                                     ast::OperatorPrecedence::kBitwiseAnd);
        auto newExpr =
            ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(right));
        expr = zc::mv(newExpr);
      }
    }
    return zc::mv(expr);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::Expression>> Parser::parseEqualityExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseEqualityExpression");

  // equalityExpression:
  //   relationalExpression (equalityOperator relationalExpression)*;
  //
  // Handles equality and inequality comparisons with left-to-right associativity

  ZC_IF_SOME(expr, parseRelationalExpression()) {
    while (expectToken(lexer::TokenKind::kEqualsEquals) ||
           expectToken(lexer::TokenKind::kExclamationEquals)) {
      zc::String opText = impl->peekToken().getText(impl->sourceMgr);
      impl->consumeToken();

      ZC_IF_SOME(right, parseRelationalExpression()) {
        // Create binary expression AST node
        auto op =
            ast::factory::createBinaryOperator(zc::mv(opText), ast::OperatorPrecedence::kEquality);
        auto newExpr =
            ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(right));
        expr = zc::mv(newExpr);
      }
    }
    return zc::mv(expr);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::Expression>> Parser::parseRelationalExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseRelationalExpression");

  // relationalExpression:
  //   shiftExpression (relationalOperator shiftExpression)*;
  //
  // Handles relational comparisons (<, >, <=, >=) with left-to-right associativity

  ZC_IF_SOME(expr, parseShiftExpression()) {
    while (expectToken(lexer::TokenKind::kLessThan) ||
           expectToken(lexer::TokenKind::kGreaterThan) ||
           expectToken(lexer::TokenKind::kLessThanEquals) ||
           expectToken(lexer::TokenKind::kGreaterThanEquals)) {
      zc::String opText = impl->peekToken().getText(impl->sourceMgr);
      impl->consumeToken();

      ZC_IF_SOME(right, parseShiftExpression()) {
        // Create binary expression AST node
        auto op = ast::factory::createBinaryOperator(zc::mv(opText),
                                                     ast::OperatorPrecedence::kRelational);
        auto newExpr =
            ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(right));
        expr = zc::mv(newExpr);
      }
    }
    return zc::mv(expr);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::Expression>> Parser::parseShiftExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseShiftExpression");

  // shiftExpression:
  //   additiveExpression (shiftOperator additiveExpression)*;
  //
  // Handles bit shift operations (<<, >>) with left-to-right associativity

  ZC_IF_SOME(expr, parseAdditiveExpression()) {
    while (expectToken(lexer::TokenKind::kLessThanLessThan) ||
           expectToken(lexer::TokenKind::kGreaterThanGreaterThan)) {
      zc::String opText = impl->peekToken().getText(impl->sourceMgr);
      impl->consumeToken();

      ZC_IF_SOME(right, parseAdditiveExpression()) {
        // Create binary expression AST node
        auto op =
            ast::factory::createBinaryOperator(zc::mv(opText), ast::OperatorPrecedence::kShift);
        auto newExpr =
            ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(right));
        expr = zc::mv(newExpr);
      }
    }
    return zc::mv(expr);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::Expression>> Parser::parseAdditiveExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseAdditiveExpression");

  // additiveExpression:
  //   multiplicativeExpression ((PLUS | MINUS) multiplicativeExpression)*;
  //
  // Handles addition and subtraction with left-to-right associativity

  ZC_IF_SOME(expr, parseMultiplicativeExpression()) {
    while (expectToken(lexer::TokenKind::kPlus) || expectToken(lexer::TokenKind::kMinus)) {
      zc::String opText = impl->peekToken().getText(impl->sourceMgr);
      impl->consumeToken();

      ZC_IF_SOME(right, parseMultiplicativeExpression()) {
        // Create binary expression AST node
        auto op =
            ast::factory::createBinaryOperator(zc::mv(opText), ast::OperatorPrecedence::kAdditive);
        auto newExpr =
            ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(right));
        expr = zc::mv(newExpr);
      }
    }
    return zc::mv(expr);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::Expression>> Parser::parseMultiplicativeExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseMultiplicativeExpression");

  // multiplicativeExpression:
  //   exponentiationExpression (multiplicativeOperator exponentiationExpression)*;
  //
  // Handles multiplication, division, and modulo with left-to-right associativity

  ZC_IF_SOME(expr, parseExponentiationExpression()) {
    while (expectToken(lexer::TokenKind::kAsterisk) || expectToken(lexer::TokenKind::kSlash) ||
           expectToken(lexer::TokenKind::kPercent)) {
      zc::String opText = impl->peekToken().getText(impl->sourceMgr);
      impl->consumeToken();

      ZC_IF_SOME(right, parseExponentiationExpression()) {
        // Create binary expression AST node
        auto op = ast::factory::createBinaryOperator(zc::mv(opText),
                                                     ast::OperatorPrecedence::kMultiplicative);

        auto newExpr =
            ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(right));
        expr = zc::mv(newExpr);
      }
    }

    return zc::mv(expr);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::Expression>> Parser::parseExponentiationExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseExponentiationExpression");

  // exponentiationExpression:
  //   castExpression
  //   | updateExpression POW exponentiationExpression;
  //
  // Handles exponentiation with right-to-left associativity

  // Try to parse castExpression first
  ZC_IF_SOME(expr, parseCastExpression()) { return zc::mv(expr); }

  // Try updateExpression POW exponentiationExpression (right-associative)
  ZC_IF_SOME(left, parseUpdateExpression()) {
    const lexer::Token& token = impl->peekToken();
    if (token.is(lexer::TokenKind::kAsteriskAsterisk)) {  // POW operator
      zc::String opText = token.getText(impl->sourceMgr);
      impl->consumeToken();

      ZC_IF_SOME(right, parseExponentiationExpression()) {  // Right-associative
        auto op = ast::factory::createBinaryOperator(zc::mv(opText),
                                                     ast::OperatorPrecedence::kExponentiation,
                                                     ast::OperatorAssociativity::kRight);
        return ast::factory::createBinaryExpression(zc::mv(left), zc::mv(op), zc::mv(right));
      }
    }
    return zc::mv(left);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::Expression>> Parser::parseUnaryExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseUnaryExpression");

  const lexer::Token& token = impl->peekToken();

  // unaryExpression:
  //   updateExpression
  //   | VOID unaryExpression
  //   | TYPEOF unaryExpression
  //   | PLUS unaryExpression
  //   | MINUS unaryExpression
  //   | BIT_NOT unaryExpression
  //   | NOT unaryExpression
  //   | awaitExpression
  //   | LT type GT unaryExpression;
  //
  // Handles prefix unary operators and type assertions

  // Check for VOID operator
  if (token.is(lexer::TokenKind::kVoidKeyword)) {
    zc::String opText = token.getText(impl->sourceMgr);
    impl->consumeToken();

    ZC_IF_SOME(expr, parseUnaryExpression()) {
      auto op = ast::factory::createVoidOperator();
      auto unaryExpr = ast::factory::createPrefixUnaryExpression(zc::mv(op), zc::mv(expr));
      return zc::mv(unaryExpr);
    }
  }
  // Check for TYPEOF operator
  else if (token.is(lexer::TokenKind::kTypeOfKeyword)) {
    zc::String opText = token.getText(impl->sourceMgr);
    impl->consumeToken();

    ZC_IF_SOME(expr, parseUnaryExpression()) {
      auto op = ast::factory::createTypeOfOperator();
      auto unaryExpr = ast::factory::createPrefixUnaryExpression(zc::mv(op), zc::mv(expr));
      return zc::mv(unaryExpr);
    }
  }
  // Check for basic unary operators: +, -, !, ~
  else if (token.is(lexer::TokenKind::kPlus) || token.is(lexer::TokenKind::kMinus) ||
           token.is(lexer::TokenKind::kExclamation) || token.is(lexer::TokenKind::kTilde)) {
    zc::String opText = token.getText(impl->sourceMgr);
    impl->consumeToken();

    ZC_IF_SOME(expr, parseUnaryExpression()) {
      // Create appropriate unary operator based on token kind
      zc::Own<ast::UnaryOperator> op;
      if (opText == "+") {
        op = ast::factory::createUnaryPlusOperator();
      } else if (opText == "-") {
        op = ast::factory::createUnaryMinusOperator();
      } else if (opText == "!") {
        op = ast::factory::createLogicalNotOperator();
      } else if (opText == "~") {
        op = ast::factory::createBitwiseNotOperator();
      } else {
        op = ast::factory::createUnaryOperator(zc::mv(opText), true);
      }
      auto unaryExpr = ast::factory::createPrefixUnaryExpression(zc::mv(op), zc::mv(expr));
      return zc::mv(unaryExpr);
    }
  }
  // Check for AWAIT expression
  else if (token.is(lexer::TokenKind::kAwaitKeyword)) {
    // Delegate to parseAwaitExpression, but we need to return a UnaryExpression
    // For now, treat await as a unary operator
    zc::String opText = token.getText(impl->sourceMgr);
    impl->consumeToken();

    ZC_IF_SOME(expr, parseUnaryExpression()) {
      // For await, we should use AwaitExpression instead of UnaryExpression
      // But for now, treat it as a unary operator
      auto op = ast::factory::createUnaryOperator(zc::str("await"), true);
      auto unaryExpr = ast::factory::createPrefixUnaryExpression(zc::mv(op), zc::mv(expr));
      return zc::mv(unaryExpr);
    }
  }
  // Check for type assertion: LT type GT unaryExpression
  else if (token.is(lexer::TokenKind::kLessThan)) {
    // This is a type assertion, parse it
    impl->consumeToken();  // consume '<'

    ZC_IF_SOME(type, parseType()) {
      if (expectToken(lexer::TokenKind::kGreaterThan)) {
        impl->consumeToken();  // consume '>'

        ZC_IF_SOME(expr, parseUnaryExpression()) {
          // Create a type assertion expression
          // TODO: Implement proper type assertion AST node
          // For now, create a cast expression or similar
          (void)type;  // Suppress unused variable warning for now
          auto op = ast::factory::createUnaryOperator(zc::str("<type>"), true);
          auto unaryExpr = ast::factory::createPrefixUnaryExpression(zc::mv(op), zc::mv(expr));
          return zc::mv(unaryExpr);
        }
      }
    }
  }

  // If not a unary expression, try updateExpression
  return parseUpdateExpression();
}

zc::Maybe<zc::Own<ast::UpdateExpression>> Parser::parseUpdateExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseUpdateExpression");

  // updateExpression:
  //   leftHandSideExpression
  //   | leftHandSideExpression INC
  //   | leftHandSideExpression DEC
  //   | INC leftHandSideExpression
  //   | DEC leftHandSideExpression;

  const lexer::Token& token = impl->peekToken();

  // Check for prefix increment/decrement operators
  if (token.is(lexer::TokenKind::kPlusPlus) || token.is(lexer::TokenKind::kMinusMinus)) {
    zc::String opText = token.getText(impl->sourceMgr);
    impl->consumeToken();

    // For prefix operators, parse unaryExpression (not leftHandSideExpression)
    ZC_IF_SOME(operand, parseLeftHandSideExpressionOrHigher()) {
      zc::Own<ast::UnaryOperator> op = token.is(lexer::TokenKind::kPlusPlus)
                                           ? ast::factory::createPreIncrementOperator()
                                           : ast::factory::createPreDecrementOperator();
      return ast::factory::createPrefixUnaryExpression(zc::mv(op), zc::mv(operand));
    }

    return zc::none;
  }

  // Parse leftHandSideExpression first
  ZC_IF_SOME(expression, parseLeftHandSideExpressionOrHigher()) {
    const lexer::Token& postToken = impl->peekToken();

    // Check for postfix increment/decrement operators
    if ((postToken.is(lexer::TokenKind::kPlusPlus) ||
         postToken.is(lexer::TokenKind::kMinusMinus))) {
      zc::String opText = postToken.getText(impl->sourceMgr);
      impl->consumeToken();

      zc::Own<ast::UnaryOperator> op = token.is(lexer::TokenKind::kPlusPlus)
                                           ? ast::factory::createPostIncrementOperator()
                                           : ast::factory::createPostDecrementOperator();
      return ast::factory::createPostfixUnaryExpression(zc::mv(op), zc::mv(expression));
    }

    // No update operators found, return the expression as-is
    return zc::mv(expression);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::LeftHandSideExpression>> Parser::parseLeftHandSideExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseLeftHandSideExpression");

  // leftHandSideExpression:
  //   newExpression
  //   | callExpression
  //   | optionalExpression;
  //
  // This is the main entry point for parsing left-hand side expressions
  // We delegate to parseLeftHandSideExpressionOrHigher for the actual implementation

  return parseLeftHandSideExpressionOrHigher();
}

zc::Maybe<zc::Own<ast::PrimaryExpression>> Parser::parsePrimaryExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parsePrimaryExpression");

  // primaryExpression:
  //   THIS
  //   | identifier
  //   | literal
  //   | arrayLiteral
  //   | objectLiteral
  //   | LPAREN expression RPAREN;
  //
  // Handles basic atomic expressions

  const lexer::Token& token = impl->peekToken();

  switch (token.getKind()) {
    case lexer::TokenKind::kIdentifier:
      return parseIdentifier();

    case lexer::TokenKind::kIntegerLiteral:
    case lexer::TokenKind::kFloatLiteral:
    case lexer::TokenKind::kStringLiteral:
    case lexer::TokenKind::kTrueKeyword:
    case lexer::TokenKind::kFalseKeyword:
    case lexer::TokenKind::kNullKeyword:
    case lexer::TokenKind::kNilKeyword:
      return parseLiteralExpression();

    case lexer::TokenKind::kLeftParen: {
      impl->consumeToken();
      ZC_IF_SOME(expr, parseParenthesizedExpression()) {
        if (!consumeToken(lexer::TokenKind::kRightParen)) { return zc::none; }
        return zc::mv(expr);
      }
      return zc::none;
    }

    case lexer::TokenKind::kLeftBracket:
      return parseArrayLiteralExpression();

    case lexer::TokenKind::kLeftBrace:
      return parseObjectLiteralExpression();

    default:
      return zc::none;
  }
}

zc::Maybe<zc::Own<ast::LiteralExpression>> Parser::parseLiteralExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseLiteralExpression");

  // literal:
  //   nilLiteral
  //   | booleanLiteral
  //   | numericLiteral
  //   | stringLiteral;
  //
  // Handles all literal values (numbers, strings, booleans, nil)

  const lexer::Token& token = impl->peekToken();

  switch (token.getKind()) {
    case lexer::TokenKind::kIntegerLiteral: {
      zc::String value = token.getText(impl->sourceMgr);
      impl->consumeToken();
      double numValue = zc::StringPtr(value).parseAs<double>();
      return ast::factory::createNumericLiteral(numValue);
    }
    case lexer::TokenKind::kFloatLiteral: {
      zc::String value = token.getText(impl->sourceMgr);
      impl->consumeToken();
      double numValue = zc::StringPtr(value).parseAs<double>();
      return ast::factory::createNumericLiteral(numValue);
    }
    case lexer::TokenKind::kStringLiteral: {
      zc::String value = token.getText(impl->sourceMgr);
      impl->consumeToken();
      return ast::factory::createStringLiteral(zc::mv(value));
    }
    case lexer::TokenKind::kTrueKeyword: {
      impl->consumeToken();
      return ast::factory::createBooleanLiteral(true);
    }
    case lexer::TokenKind::kFalseKeyword: {
      impl->consumeToken();
      return ast::factory::createBooleanLiteral(false);
    }
    case lexer::TokenKind::kNullKeyword:
    case lexer::TokenKind::kNilKeyword: {
      impl->consumeToken();
      return ast::factory::createNilLiteral();
    }

    default:
      return zc::none;
  }
}

zc::Maybe<zc::Own<ast::ArrayLiteralExpression>> Parser::parseArrayLiteralExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseArrayLiteralExpression");

  // arrayLiteral:
  //   LBRACK RBRACK
  //   | LBRACK elementList RBRACK
  //   | LBRACK elementList COMMA RBRACK;
  //
  // Handles array literal expressions [1, 2, 3]

  if (!consumeToken(lexer::TokenKind::kLeftBracket)) { return zc::none; }

  zc::Vector<zc::Own<ast::Expression>> elements;

  if (!expectToken(lexer::TokenKind::kRightBracket)) {
    do {
      ZC_IF_SOME(element, parseAssignmentExpression()) { elements.add(zc::mv(element)); }
    } while (consumeToken(lexer::TokenKind::kComma));
  }

  if (!consumeToken(lexer::TokenKind::kRightBracket)) { return zc::none; }

  // TODO: Create array literal expression AST node (ArrayLiteralExpression class not yet defined)
  (void)elements;  // Suppress unused variable warning
  return zc::none;
}

zc::Maybe<zc::Own<ast::ObjectLiteralExpression>> Parser::parseObjectLiteralExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseObjectLiteralExpression");

  // objectLiteral:
  //   LBRACE RBRACE
  //   | LBRACE propertyDefinitionList RBRACE
  //   | LBRACE propertyDefinitionList COMMA RBRACE;
  //
  // Handles object literal expressions {key: value}

  if (!consumeToken(lexer::TokenKind::kLeftBrace)) { return zc::none; }

  // TODO: Parse object properties

  if (!consumeToken(lexer::TokenKind::kRightBrace)) { return zc::none; }

  // TODO: Create object literal expression AST node (ObjectLiteralExpression class not yet defined)
  return zc::none;
}

// ================================================================================
// Type parsing implementations

zc::Maybe<zc::Own<ast::Type>> Parser::parseType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseType");

  // type:
  //   unionType
  //   | intersectionType
  //   | primaryType
  //   | functionType
  //   | arrayType
  //   | tupleType
  //   | objectType
  //   | typeReference
  //   | optionalType;
  //
  // Handle optional types

  if (expectToken(lexer::TokenKind::kQuestion)) {
    impl->consumeToken();
    // TODO: Handle optional types properly with a dedicated AST node
  }

  ZC_IF_SOME(type, parseUnionType()) {
    // TODO: Handle optional types properly with a dedicated AST node
    return zc::mv(type);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::TypeAnnotation>> Parser::parseTypeAnnotation() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseTypeAnnotation");

  if (!consumeToken(lexer::TokenKind::kColon)) { return zc::none; }

  ZC_IF_SOME(type, parseType()) { return ast::factory::createTypeAnnotation(zc::mv(type)); }

  return zc::none;
}

zc::Maybe<zc::Own<ast::UnionType>> Parser::parseUnionType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseUnionType");

  // unionType:
  //   intersectionType (PIPE intersectionType)*;
  //
  // Handles union types like A | B | C

  ZC_IF_SOME(type, parseIntersectionType()) {
    zc::Vector<zc::Own<ast::Type>> types;
    types.add(zc::mv(type));

    while (expectToken(lexer::TokenKind::kBar)) {
      impl->consumeToken();

      ZC_IF_SOME(rightType, parseIntersectionType()) { types.add(zc::mv(rightType)); }
    }

    if (types.size() == 1) {
      // Single type, return as UnionType with one element
      return ast::factory::createUnionType(zc::mv(types));
    }

    return ast::factory::createUnionType(zc::mv(types));
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::IntersectionType>> Parser::parseIntersectionType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseIntersectionType");

  // intersectionType:
  //   primaryType (AMPERSAND primaryType)*;
  //
  // Handles intersection types like A & B & C

  ZC_IF_SOME(type, parsePrimaryType()) {
    zc::Vector<zc::Own<ast::Type>> types;
    types.add(zc::mv(type));

    while (expectToken(lexer::TokenKind::kAmpersand)) {
      impl->consumeToken();

      ZC_IF_SOME(rightType, parsePrimaryType()) { types.add(zc::mv(rightType)); }
    }

    if (types.size() == 1) {
      // Single type, return as IntersectionType with one element
      return ast::factory::createIntersectionType(zc::mv(types));
    }

    return ast::factory::createIntersectionType(zc::mv(types));
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::Type>> Parser::parsePrimaryType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parsePrimaryType");

  // primaryType:
  //   parenthesizedType
  //   | predefinedType
  //   | typeReference
  //   | objectType
  //   | arrayType
  //   | tupleType;
  //
  // Handles basic type expressions

  const lexer::Token& token = impl->peekToken();

  switch (token.getKind()) {
    case lexer::TokenKind::kLeftParen:
      // Parenthesized type or tuple type
      return parseParenthesizedType();

    case lexer::TokenKind::kLeftBrace:
      return parseObjectType();

    case lexer::TokenKind::kIdentifier:
      return parseTypeReference();

    default:
      return parsePredefinedType();
  }
}

zc::Maybe<zc::Own<ast::ArrayType>> Parser::parseArrayType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseArrayType");

  // arrayType:
  //   primaryType LBRACK RBRACK;
  //
  // Handles array types like T[]

  ZC_IF_SOME(elementType, parsePrimaryType()) {
    zc::Own<ast::Type> result = zc::mv(elementType);

    while (expectToken(lexer::TokenKind::kLeftBracket)) {
      impl->consumeToken();

      if (!consumeToken(lexer::TokenKind::kRightBracket)) { return zc::none; }

      result = ast::factory::createArrayType(zc::mv(result));
    }

    return ast::factory::createArrayType(zc::mv(result));
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::FunctionType>> Parser::parseFunctionType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseFunctionType");

  // functionType:
  //   typeParameters? LPAREN parameterList? RPAREN ARROW type;
  //
  // Handles function types like (a: T, b: U) -> R

  // TODO: Implement function type parsing
  return zc::none;
}

zc::Maybe<zc::Own<ast::ParenthesizedType>> Parser::parseParenthesizedType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseParenthesizedType");

  if (!consumeToken(lexer::TokenKind::kLeftParen)) { return zc::none; }

  ZC_IF_SOME(type, parseType()) {
    if (!consumeToken(lexer::TokenKind::kRightParen)) { return zc::none; }
    return ast::factory::createParenthesizedType(zc::mv(type));
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::ObjectType>> Parser::parseObjectType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseObjectType");

  // objectType:
  //   LBRACE typeMemberList? RBRACE;
  //
  // Handles object types like {prop: T, method(): U}

  if (!consumeToken(lexer::TokenKind::kLeftBrace)) { return zc::none; }

  zc::Vector<zc::Own<ast::Node>> members;

  // TODO: Parse object type members

  if (!consumeToken(lexer::TokenKind::kRightBrace)) { return zc::none; }

  return ast::factory::createObjectType(zc::mv(members));
}

zc::Maybe<zc::Own<ast::TupleType>> Parser::parseTupleType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseTupleType");

  // tupleType:
  //   LBRACK tupleElementTypes? RBRACK;
  //
  // Handles tuple types like [T, U, V]

  if (!consumeToken(lexer::TokenKind::kLeftParen)) { return zc::none; }

  zc::Vector<zc::Own<ast::Type>> elementTypes;

  if (!expectToken(lexer::TokenKind::kRightParen)) {
    do {
      ZC_IF_SOME(elementType, parseType()) { elementTypes.add(zc::mv(elementType)); }
    } while (consumeToken(lexer::TokenKind::kComma));
  }

  if (!consumeToken(lexer::TokenKind::kRightParen)) { return zc::none; }

  return ast::factory::createTupleType(zc::mv(elementTypes));
}

zc::Maybe<zc::Own<ast::TypeReference>> Parser::parseTypeReference() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseTypeReference");

  // typeReference:
  //   typeName typeArguments?;
  //
  // Handles type references like MyType<T, U>

  ZC_IF_SOME(typeName, parseIdentifier()) {
    // TODO: Handle type arguments
    return ast::factory::createTypeReference(zc::mv(typeName));
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::PredefinedType>> Parser::parsePredefinedType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parsePredefinedType");

  // predefinedType:
  //   BOOL | I8 | I32 | I64 | U8 | U16 | U32 | U64 | F32 | F64 | STR | UNIT | NIL;
  //
  // Handles built-in primitive types

  const lexer::Token& token = impl->peekToken();

  switch (token.getKind()) {
    case lexer::TokenKind::kBoolKeyword:
    case lexer::TokenKind::kI8Keyword:
    case lexer::TokenKind::kI32Keyword:
    case lexer::TokenKind::kI64Keyword:
    case lexer::TokenKind::kU8Keyword:
    case lexer::TokenKind::kU16Keyword:
    case lexer::TokenKind::kU32Keyword:
    case lexer::TokenKind::kU64Keyword:
    case lexer::TokenKind::kF32Keyword:
    case lexer::TokenKind::kF64Keyword:
    case lexer::TokenKind::kStrKeyword:
    case lexer::TokenKind::kUnitKeyword:
    case lexer::TokenKind::kNilKeyword: {
      zc::String typeName = token.getText(impl->sourceMgr);
      impl->consumeToken();
      return ast::factory::createPredefinedType(zc::mv(typeName));
    }

    default:
      return zc::none;
  }
}

zc::Maybe<zc::Own<ast::Expression>> Parser::parseCoalesceExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseCoalesceExpression");

  // coalesceExpression: bitwiseORExpression (NULL_COALESCE bitwiseORExpression)*;

  ZC_IF_SOME(expr, parseBitwiseOrExpression()) {
    while (expectToken(lexer::TokenKind::kQuestionQuestion)) {
      zc::String opText = impl->peekToken().getText(impl->sourceMgr);
      impl->consumeToken();

      ZC_IF_SOME(right, parseBitwiseOrExpression()) {
        // Create binary expression AST node
        auto op =
            ast::factory::createBinaryOperator(zc::mv(opText), ast::OperatorPrecedence::kLogicalOr);
        auto newExpr =
            ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(right));
        expr = zc::mv(newExpr);
      }
    }
    return zc::mv(expr);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::CastExpression>> Parser::parseCastExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseCastExpression");

  // castExpression: unaryExpression (AS (QUESTION | NOT)? type)*;

  ZC_IF_SOME(expr, parseUnaryExpression()) {
    while (expectToken(lexer::TokenKind::kAsKeyword)) {
      impl->consumeToken();

      bool isOptional = false;
      bool isNonNull = false;

      if (expectToken(lexer::TokenKind::kQuestion)) {
        isOptional = true;
        impl->consumeToken();
      } else if (expectToken(lexer::TokenKind::kExclamation)) {
        isNonNull = true;
        impl->consumeToken();
      }

      ZC_IF_SOME(type, parseType()) {
        zc::String targetType = zc::str("unknown");  // TODO: Extract type name from AST
        // Use type and isNonNull to avoid unused variable warning
        (void)type;
        (void)isNonNull;
        return ast::factory::createCastExpression(zc::mv(expr), zc::mv(targetType), isOptional);
      }
    }

    // If no cast, return the unary expression as a cast expression
    // This might need adjustment based on the actual AST hierarchy
    return zc::none;
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::AwaitExpression>> Parser::parseAwaitExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseAwaitExpression");

  // awaitExpression: AWAIT unaryExpression;

  if (!expectToken(lexer::TokenKind::kAwaitKeyword)) { return zc::none; }

  impl->consumeToken();

  ZC_IF_SOME(expr, parseUnaryExpression()) {
    return ast::factory::createAwaitExpression(zc::mv(expr));
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::DebuggerStatement>> Parser::parseDebuggerStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseDebuggerStatement");

  source::SourceLoc startLoc = impl->peekToken().getLocation();
  if (!consumeToken(lexer::TokenKind::kDebuggerKeyword)) { return zc::none; }

  if (!consumeToken(lexer::TokenKind::kSemicolon)) { return zc::none; }
  source::SourceLoc endLoc = impl->peekToken().getLocation();

  auto debuggerStmt = ast::factory::createDebuggerStatement();
  debuggerStmt->setSourceRange(source::SourceRange(startLoc, endLoc));
  return debuggerStmt;
}

zc::Maybe<zc::Own<ast::NewExpression>> Parser::parseNewExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseNewExpression");

  // newExpression: memberExpression | NEW newExpression;
  // memberExpression: (primaryExpression | superProperty | NEW memberExpression arguments)
  //                   (LBRACK expression RBRACK | PERIOD identifier)*;

  if (!expectToken(lexer::TokenKind::kNewKeyword)) { return zc::none; }

  impl->consumeToken();

  // Parse the expression part using parseMemberExpressionRest
  zc::Maybe<zc::Own<ast::LeftHandSideExpression>> expression = zc::none;

  // Parse primary expression first
  ZC_IF_SOME(primaryExpr, parsePrimaryExpression()) {
    // Use parseMemberExpressionRest to handle member access chains
    // allowOptionalChain is set to false for new expressions
    ZC_IF_SOME(memberExpr, parseMemberExpressionRest(zc::mv(primaryExpr), false)) {
      expression = zc::mv(memberExpr);
    }
    else { return zc::none; }
  }
  else { return zc::none; }

  // Check for invalid optional chain from new expression
  if (expectToken(lexer::TokenKind::kQuestionDot)) {
    // TODO: Add proper diagnostic error reporting
    // parseErrorAtCurrentToken(Diagnostics.Invalid_optional_chain_from_new_expression_Did_you_mean_to_call_0,
    // getTextOfNodeFromSourceText(sourceText, expression));
    return zc::none;
  }

  // Parse optional arguments (argumentList)
  zc::Vector<zc::Own<ast::Expression>> arguments;
  if (expectToken(lexer::TokenKind::kLeftParen)) {
    ZC_IF_SOME(argList, parseArgumentList()) { arguments = zc::mv(argList); }
    else { return zc::none; }
  }

  ZC_IF_SOME(expr, expression) {
    return ast::factory::createNewExpression(zc::mv(expr), zc::mv(arguments));
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::ParenthesizedExpression>> Parser::parseParenthesizedExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseParenthesizedExpression");

  // parenthesizedExpression: LPAREN expression RPAREN;

  if (!expectToken(lexer::TokenKind::kLeftParen)) { return zc::none; }

  impl->consumeToken();

  ZC_IF_SOME(expr, parseExpression()) {
    if (!consumeToken(lexer::TokenKind::kRightParen)) { return zc::none; }

    // Create a parenthesized expression with the parsed expression
    return ast::factory::createParenthesizedExpression(zc::mv(expr));
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::MemberExpression>> Parser::parseMemberExpressionRest(
    zc::Own<ast::MemberExpression> expr, bool allowOptionalChain) {
  while (true) {
    bool questionDotToken = false;
    bool isPropertyAccess = false;

    // Check for optional chaining first
    if (allowOptionalChain && expectToken(lexer::TokenKind::kQuestionDot)) {
      impl->consumeToken();  // consume '?.'
      questionDotToken = true;
      // After ?., check if next token is identifier (property access) or '[' (element access)
      isPropertyAccess = expectToken(lexer::TokenKind::kIdentifier);
    } else {
      // Check for regular property access
      isPropertyAccess = expectToken(lexer::TokenKind::kPeriod);
      if (isPropertyAccess) {
        impl->consumeToken();  // consume '.'
      }
    }

    if (isPropertyAccess) {
      // Property access: obj.prop or obj?.prop
      ZC_IF_SOME(name, parseIdentifier()) {
        expr = ast::factory::createPropertyAccessExpression(zc::mv(expr), zc::mv(name),
                                                            questionDotToken);
        continue;
      }
      else { return zc::none; }
    }

    // Check for element access: obj[expr] or obj?.[expr]
    if (expectToken(lexer::TokenKind::kLeftBracket)) {
      impl->consumeToken();  // consume '['
      ZC_IF_SOME(index, parseExpression()) {
        if (!consumeToken(lexer::TokenKind::kRightBracket)) { return zc::none; }
        expr = ast::factory::createElementAccessExpression(zc::mv(expr), zc::mv(index),
                                                           questionDotToken);
        continue;
      }
      else { return zc::none; }
    }

    // If we had a questionDotToken but couldn't parse property or element access, it's an error
    if (questionDotToken) { return zc::none; }

    // No more member expressions
    break;
  }

  return zc::mv(expr);
}

zc::Maybe<zc::Own<ast::MemberExpression>> Parser::parseSuperExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseSuperExpression");

  if (!expectToken(lexer::TokenKind::kSuperKeyword)) { return zc::none; }

  impl->consumeToken();  // consume 'super'

  // Create the super identifier as the base expression
  auto expression = ast::factory::createIdentifier(zc::str("super"));

  // Check for type arguments (e.g., super<T>)
  if (expectToken(lexer::TokenKind::kLessThan)) {
    source::SourceLoc errorLoc = impl->peekToken().getLocation();
    impl->diagnosticEngine.diagnose<diagnostics::DiagID::InvalidChar>(
        errorLoc, zc::str("super may not use type arguments"));
  }

  // Check what follows the super keyword
  if (expectToken(lexer::TokenKind::kLeftParen) || expectToken(lexer::TokenKind::kPeriod) ||
      expectToken(lexer::TokenKind::kLeftBracket)) {
    // Valid super usage - return the base expression
    // The caller will handle member access or call expressions
    return zc::mv(expression);
  }

  // If we reach here, super must be followed by '(', '.', or '['
  // Report an error and try to recover by parsing a dot
  source::SourceLoc errorLoc = impl->peekToken().getLocation();
  impl->diagnosticEngine.diagnose<diagnostics::DiagID::InvalidChar>(
      errorLoc, zc::str("super must be followed by an argument list or member access"));

  // Try to recover by expecting a dot and parsing the right side
  if (expectToken(lexer::TokenKind::kPeriod)) {
    impl->consumeToken();  // consume '.'
    ZC_IF_SOME(property, parseIdentifier()) {
      return ast::factory::createPropertyAccessExpression(zc::mv(expression), zc::mv(property),
                                                          false);
    }
  }

  return zc::mv(expression);
}

const lexer::Token& Parser::lookAhead(unsigned n) const { return impl->lookAheadToken(n); }

bool Parser::canLookAhead(unsigned n) const { return impl->canLookAheadToken(n); }

bool Parser::isLookAhead(unsigned n, lexer::TokenKind kind) const {
  return impl->isLookAheadToken(n, kind);
}

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
