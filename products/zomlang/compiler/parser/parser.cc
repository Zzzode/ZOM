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
#include "zc/core/vector.h"
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
        lexer(sourceMgr, diagnosticEngine, langOpts, bufferId) {}
  ~Impl() noexcept(false) = default;

  ZC_DISALLOW_COPY_AND_MOVE(Impl);

  /// Helper to get the next token from the lexer
  void consumeToken() { lexer.lex(currentToken); }
  /// Helper to look at the current token
  const lexer::Token& peekToken() const { return currentToken; }
  /// Helper to get the current token
  const lexer::Token& getCurrentToken() const { return currentToken; }

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
  const lexer::Token& token = currentToken();
  switch (context) {
    case ParsingContext::kSourceElements:
      return token.is(lexer::TokenKind::kEOF);
    default:
      return false;
  }
}

bool Parser::isListElement(ParsingContext context, bool inErrorRecovery) const {
  const lexer::Token& token = currentToken();
  switch (context) {
    case ParsingContext::kSourceElements:
      return !token.is(lexer::TokenKind::kSemicolon) && isStartOfStatement();
    default:
      return false;
  }
}

bool Parser::abortParsingListOrMoveToNextToken(ParsingContext context) {
  trace::traceEvent(trace::TraceCategory::kParser, "Error recovery", "Skipping token");

  // Simple error recovery: skip the current token and try again
  consumeToken();
  return false;  // Continue parsing
}

zc::Maybe<zc::Own<ast::Node>> Parser::parse() {
  trace::FunctionTracer functionTracer(trace::TraceCategory::kParser, __FUNCTION__);

  consumeToken();
  ZC_IF_SOME(sourceFileNode, parseSourceFile()) {
    trace::traceEvent(trace::TraceCategory::kParser, "Parse completed successfully");
    return zc::mv(sourceFileNode);
  }

  trace::traceEvent(trace::TraceCategory::kParser, "Parse failed");

  return zc::none;
}

zc::Maybe<zc::Own<ast::TypeQuery>> Parser::parseTypeQuery() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseTypeQuery");

  // Parse type query according to the grammar rule:
  // typeQuery: TYPEOF typeQueryExpression
  // This handles type queries like typeof myVar or typeof MyClass.prop

  const lexer::Token& token = currentToken();
  const source::SourceLoc startLoc = token.getLocation();

  if (!token.is(lexer::TokenKind::kTypeOfKeyword)) { return zc::none; }

  consumeToken();  // consume 'typeof'

  ZC_IF_SOME(queryExpr, parseTypeQueryExpression()) {
    return finishNode(ast::factory::createTypeQuery(zc::mv(queryExpr)), startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::Expression>> Parser::parseTypeQueryExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseTypeQueryExpression");

  // Parse type query expression according to the grammar rule:
  // typeQueryExpression: identifier (PERIOD identifier)*
  // This handles expressions like 'MyClass' or 'MyClass.field' in typeof queries

  ZC_IF_SOME(firstId, parseIdentifier()) {
    zc::Own<ast::LeftHandSideExpression> result = zc::mv(firstId);

    // Parse additional identifiers separated by periods
    while (expectToken(lexer::TokenKind::kPeriod)) {
      consumeToken();  // consume '.'

      ZC_IF_SOME(nextId, parseIdentifier()) {
        // Use PropertyAccessExpression for member access
        auto propAccess =
            ast::factory::createPropertyAccessExpression(zc::mv(result), zc::mv(nextId), false);
        result = zc::mv(propAccess);
      }
      else {
        // Error: expected identifier after '.'
        return zc::none;
      }
    }

    return result;
  }

  return zc::none;
}

zc::Maybe<zc::Vector<zc::Own<ast::Type>>> Parser::parseRaisesClause() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseRaisesClause");

  // Parse raises clause according to the grammar rule:
  // raisesClause: RAISES type
  // This handles error type specifications in function types and declarations
  // Example: (x: i32) -> i32 raises ErrorType

  zc::Vector<zc::Own<ast::Type>> errorTypes;

  // Parse the first error type
  ZC_IF_SOME(errorType, parseType()) {
    errorTypes.add(zc::mv(errorType));

    // Parse additional error types separated by commas
    while (consumeExpectedToken(lexer::TokenKind::kComma)) {
      ZC_IF_SOME(nextErrorType, parseType()) { errorTypes.add(zc::mv(nextErrorType)); }
      else {
        // Error: expected type after comma
        return zc::none;
      }
    }

    return zc::mv(errorTypes);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::SourceFile>> Parser::parseSourceFile() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseSourceFile");

  // sourceFile: module;
  // module: moduleBody?;
  // moduleBody: moduleItemList;
  // moduleItemList: moduleItem+;

  source::SourceLoc startLoc = currentToken().getLocation();

  zc::Vector<zc::Own<ast::Statement>> statements = parseList<ast::Statement>(
      ParsingContext::kSourceElements, ZC_BIND_METHOD(*this, parseModuleItem));

  trace::traceCounter(trace::TraceCategory::kParser, "Module items parsed"_zc,
                      zc::str(statements.size()));

  // Create the source file node
  zc::StringPtr fileName = impl->sourceMgr.getIdentifierForBuffer(impl->bufferId);
  zc::Own<ast::SourceFile> sourceFile =
      finishNode(ast::factory::createSourceFile(zc::str(fileName), zc::mv(statements)), startLoc);

  trace::traceEvent(trace::TraceCategory::kParser, "Source file created"_zc, fileName);
  return sourceFile;
}

zc::Maybe<zc::Own<ast::Statement>> Parser::parseModuleItem() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseModuleItem");

  // moduleItem:
  //   statementListItem
  //   | exportDeclaration
  //   | importDeclaration;

  // Check for import declaration
  if (expectToken(lexer::TokenKind::kImportKeyword)) {
    ZC_IF_SOME(importDecl, parseImportDeclaration()) { return zc::mv(importDecl); }
  }

  // Check for export declaration
  if (expectToken(lexer::TokenKind::kExportKeyword)) {
    ZC_IF_SOME(exportDecl, parseExportDeclaration()) { return zc::mv(exportDecl); }
  }

  // Otherwise, parse as statement (statementListItem)
  return parseStatement();
}

zc::Maybe<zc::Own<ast::ImportDeclaration>> Parser::parseImportDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseImportDeclaration");

  // importDeclaration: IMPORT modulePath ( AS identifierName )?;
  const lexer::Token& token = currentToken();

  // Expect IMPORT token
  if (!token.is(lexer::TokenKind::kImportKeyword)) { return zc::none; }

  source::SourceLoc startLoc = token.getLocation();
  consumeToken();  // consume IMPORT

  // Parse modulePath
  ZC_IF_SOME(modulePath, parseModulePath()) {
    zc::Maybe<zc::String> alias = zc::none;

    // Check for optional AS clause
    const lexer::Token& nextToken = currentToken();
    if (nextToken.is(lexer::TokenKind::kAsKeyword)) {
      consumeToken();  // consume AS

      // Parse identifier name (alias)
      const lexer::Token& aliasToken = currentToken();
      if (aliasToken.is(lexer::TokenKind::kIdentifier)) {
        alias = aliasToken.getText(impl->sourceMgr);
        consumeToken();  // consume identifier
      } else {
        // Error: expected identifier after AS
        return zc::none;
      }
    }

    // Create ImportDeclaration with modulePath and optional alias
    return finishNode(ast::factory::createImportDeclaration(zc::mv(modulePath), zc::mv(alias)),
                      startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::ModulePath>> Parser::parseModulePath() {
  // modulePath: bindingIdentifier ( PERIOD bindingIdentifier )*;
  const lexer::Token& token = currentToken();

  // Expect first bindingIdentifier
  if (!token.is(lexer::TokenKind::kIdentifier)) { return zc::none; }

  source::SourceLoc startLoc = token.getLocation();
  zc::Vector<zc::String> identifiers;
  identifiers.add(token.getText(impl->sourceMgr));
  consumeToken();  // consume first identifier

  // Parse optional additional identifiers separated by PERIOD
  while (true) {
    const lexer::Token& nextToken = currentToken();
    if (nextToken.is(lexer::TokenKind::kPeriod)) {
      consumeToken();  // consume PERIOD

      const lexer::Token& idToken = currentToken();
      if (idToken.is(lexer::TokenKind::kIdentifier)) {
        identifiers.add(idToken.getText(impl->sourceMgr));
        consumeToken();  // consume identifier
      } else {
        // Error: expected identifier after period
        return zc::none;
      }
    } else {
      break;  // No more periods, done parsing module path
    }
  }

  // Create ModulePath with collected identifiers
  return finishNode(ast::factory::createModulePath(zc::mv(identifiers)), startLoc);
}

zc::Maybe<zc::Own<ast::ExportDeclaration>> Parser::parseExportDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseExportDeclaration");

  // exportDeclaration: EXPORT (exportModule | exportRename);
  // exportModule: bindingIdentifier;
  // exportRename: bindingIdentifier AS bindingIdentifier FROM modulePath;
  const lexer::Token& token = currentToken();

  // Expect EXPORT token
  if (!token.is(lexer::TokenKind::kExportKeyword)) { return zc::none; }

  source::SourceLoc startLoc = token.getLocation();
  consumeToken();  // consume EXPORT

  const lexer::Token& nextToken = currentToken();
  if (nextToken.is(lexer::TokenKind::kIdentifier)) {
    zc::String identifier = nextToken.getText(impl->sourceMgr);
    consumeToken();  // consume identifier

    // Check if this is exportRename (identifier AS identifier FROM modulePath)
    const lexer::Token& followingToken = currentToken();
    if (followingToken.is(lexer::TokenKind::kAsKeyword)) {
      consumeToken();  // consume AS

      const lexer::Token& secondIdToken = currentToken();
      if (secondIdToken.is(lexer::TokenKind::kIdentifier)) {
        zc::String alias = secondIdToken.getText(impl->sourceMgr);
        consumeToken();  // consume second identifier

        const lexer::Token& fromToken = currentToken();
        if (fromToken.is(lexer::TokenKind::kFromKeyword)) {
          consumeToken();  // consume FROM

          // Parse modulePath
          ZC_IF_SOME(modulePath, parseModulePath()) {
            // Create ExportDeclaration with rename info
            return finishNode(ast::factory::createExportDeclaration(
                                  zc::mv(identifier), zc::mv(alias), zc::mv(modulePath)),
                              startLoc);
          }
        }
      }
    } else {
      // Simple exportModule: just bindingIdentifier
      return finishNode(ast::factory::createExportDeclaration(zc::mv(identifier)), startLoc);
    }
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::Statement>> Parser::parseStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseStatement");

  // statementListItem: statement | declaration;
  // statement:
  //   blockStatement
  //   | emptyStatement
  //   | expressionStatement
  //   | ifStatement
  //   | matchStatement
  //   | breakableStatement
  //   | continueStatement
  //   | breakStatement
  //   | returnStatement
  //   | debuggerStatement;

  // Check for different statement types
  switch (currentToken().getKind()) {
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
  const lexer::Token& token = currentToken();

  switch (token.getKind()) {
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
  const lexer::Token& token = currentToken();

  switch (token.getKind()) {
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

  const lexer::Token& token = currentToken();

  switch (token.getKind()) {
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
  const lexer::Token& token = currentToken();

  switch (token.getKind()) {
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

ZC_ALWAYS_INLINE(const lexer::Token& Parser::currentToken() const) { return impl->peekToken(); }

ZC_ALWAYS_INLINE(void Parser::consumeToken()) { impl->consumeToken(); }

ZC_ALWAYS_INLINE(bool Parser::expectToken(lexer::TokenKind kind)) {
  const lexer::Token& token = currentToken();
  return token.is(kind);
}

ZC_ALWAYS_INLINE(bool Parser::consumeExpectedToken(lexer::TokenKind kind)) {
  if (expectToken(kind)) {
    consumeToken();
    return true;
  }
  return false;
}

bool Parser::isUpdateExpression(lexer::TokenKind tokenKind) const {
  // Check if the current token can possibly be an increment expression.
  // This function is called inside parseUnaryExpression to decide
  // whether to call parseSimpleUnaryExpression or call parseUpdateExpression directly
  switch (tokenKind) {
    case lexer::TokenKind::kPlus:
    case lexer::TokenKind::kMinus:
    case lexer::TokenKind::kTilde:
    case lexer::TokenKind::kExclamation:
    case lexer::TokenKind::kDeleteKeyword:
    case lexer::TokenKind::kTypeOfKeyword:
    case lexer::TokenKind::kVoidKeyword:
    case lexer::TokenKind::kAwaitKeyword:
      return false;
    case lexer::TokenKind::kLessThan:
      // < can be used for generic type arguments or comparison operators
      // Both are not unary expressions, so this should be handled as update expression
      return true;
    default:
      return true;
  }
}

zc::Maybe<zc::Vector<zc::Own<ast::Expression>>> Parser::parseArgumentList() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseArgumentList");

  // argumentList:
  //   (assignmentExpression | ELLIPSIS assignmentExpression) (
  //     COMMA (assignmentExpression | ELLIPSIS assignmentExpression)
  //   )*;

  if (!expectToken(lexer::TokenKind::kLeftParen)) { return zc::none; }

  consumeToken();  // consume '('

  zc::Vector<zc::Own<ast::Expression>> arguments;

  if (!expectToken(lexer::TokenKind::kRightParen)) {
    do {
      ZC_IF_SOME(arg, parseAssignmentExpressionOrHigher()) { arguments.add(zc::mv(arg)); }
      else { return zc::none; }
    } while (consumeExpectedToken(lexer::TokenKind::kComma));
  }

  if (!consumeExpectedToken(lexer::TokenKind::kRightParen)) { return zc::none; }

  return zc::mv(arguments);
}

zc::Maybe<zc::Vector<zc::Own<ast::Type>>> Parser::parseTypeArgumentsInExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseTypeArgumentsInExpression");

  // typeArguments: LT typeArgumentList GT;
  // typeArgumentList: type (COMMA type)*;
  // This function parses type arguments in expression context, like f<number>(42)

  // Check if we have a '<' token that could start type arguments
  if (!expectToken(lexer::TokenKind::kLessThan)) { return zc::none; }

  consumeToken();  // consume '<'

  zc::Vector<zc::Own<ast::Type>> typeArguments;

  if (!expectToken(lexer::TokenKind::kGreaterThan)) {
    do {
      ZC_IF_SOME(typeArg, parseType()) { typeArguments.add(zc::mv(typeArg)); }
      else { return zc::none; }
    } while (consumeExpectedToken(lexer::TokenKind::kComma));
  }

  if (!consumeExpectedToken(lexer::TokenKind::kGreaterThan)) { return zc::none; }

  // Check if the type argument list is followed by tokens that indicate
  // this should be treated as type arguments rather than comparison operators
  const lexer::Token& nextToken = currentToken();
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
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseIdentifier");

  // bindingIdentifier: identifier
  // identifier: identifierName
  //   where identifierName must not be a reserved word

  const lexer::Token& token = currentToken();
  if (token.is(lexer::TokenKind::kIdentifier)) {
    const source::SourceLoc startLoc = token.getLocation();
    zc::String identifier = token.getText(impl->sourceMgr);
    consumeToken();

    return finishNode(ast::factory::createIdentifier(zc::mv(identifier)), startLoc);
  }
  return zc::none;
}

zc::Maybe<zc::Own<ast::Identifier>> Parser::parseBindingIdentifier() { return parseIdentifier(); }

zc::Maybe<zc::Own<ast::BindingElement>> Parser::parseBindingElement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseBindingElement");

  // bindingElement: bindingIdentifier typeAnnotation? initializer?;

  const source::SourceLoc startLoc = currentToken().getLocation();

  ZC_IF_SOME(name, parseBindingIdentifier()) {
    // Optional type annotation
    zc::Maybe<zc::Own<ast::Type>> type = parseTypeAnnotation();
    // Optional initializer
    zc::Maybe<zc::Own<ast::Expression>> initializer = parseInitializer();

    return finishNode(
        ast::factory::createBindingElement(zc::mv(name), zc::mv(type), zc::mv(initializer)),
        startLoc);
  }

  return zc::none;
}

// ================================================================================
// Statement parsing implementations

zc::Maybe<zc::Own<ast::BlockStatement>> Parser::parseBlockStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseBlockStatement");

  source::SourceLoc startLoc = currentToken().getLocation();
  if (!consumeExpectedToken(lexer::TokenKind::kLeftBrace)) { return zc::none; }

  zc::Vector<zc::Own<ast::Statement>> statements;

  while (!expectToken(lexer::TokenKind::kRightBrace) && !expectToken(lexer::TokenKind::kEOF)) {
    ZC_IF_SOME(stmt, parseStatement()) { statements.add(zc::mv(stmt)); }
    else {
      // Error recovery: skip token
      consumeToken();
    }
  }

  if (!consumeExpectedToken(lexer::TokenKind::kRightBrace)) { return zc::none; }

  // Create block statement AST node
  return finishNode(ast::factory::createBlockStatement(zc::mv(statements)), startLoc);
}

zc::Maybe<zc::Own<ast::EmptyStatement>> Parser::parseEmptyStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseEmptyStatement");

  source::SourceLoc startLoc = currentToken().getLocation();
  if (!consumeExpectedToken(lexer::TokenKind::kSemicolon)) { return zc::none; }

  // Create empty statement AST node
  return finishNode(ast::factory::createEmptyStatement(), startLoc);
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

  source::SourceLoc startLoc = currentToken().getLocation();
  ZC_IF_SOME(expr, parseExpression()) {
    // Expect semicolon
    if (!consumeExpectedToken(lexer::TokenKind::kSemicolon)) { return zc::none; }

    // Create expression statement AST node
    return finishNode(ast::factory::createExpressionStatement(zc::mv(expr)), startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::IfStatement>> Parser::parseIfStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseIfStatement");

  source::SourceLoc startLoc = currentToken().getLocation();
  if (!consumeExpectedToken(lexer::TokenKind::kIfKeyword)) { return zc::none; }

  if (!consumeExpectedToken(lexer::TokenKind::kLeftParen)) { return zc::none; }

  ZC_IF_SOME(condition, parseExpression()) {
    if (!consumeExpectedToken(lexer::TokenKind::kRightParen)) { return zc::none; }

    ZC_IF_SOME(thenStmt, parseStatement()) {
      zc::Maybe<zc::Own<ast::Statement>> elseStmt = zc::none;

      if (expectToken(lexer::TokenKind::kElseKeyword)) {
        consumeToken();
        elseStmt = parseStatement();
      }

      // Create if statement AST node
      return finishNode(
          ast::factory::createIfStatement(zc::mv(condition), zc::mv(thenStmt), zc::mv(elseStmt)),
          startLoc);
    }
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::WhileStatement>> Parser::parseWhileStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseWhileStatement");

  source::SourceLoc startLoc = currentToken().getLocation();
  if (!consumeExpectedToken(lexer::TokenKind::kWhileKeyword)) { return zc::none; }

  if (!consumeExpectedToken(lexer::TokenKind::kLeftParen)) { return zc::none; }

  ZC_IF_SOME(condition, parseExpression()) {
    if (!consumeExpectedToken(lexer::TokenKind::kRightParen)) { return zc::none; }

    ZC_IF_SOME(body, parseStatement()) {
      // Create while statement AST node
      return finishNode(ast::factory::createWhileStatement(zc::mv(condition), zc::mv(body)),
                        startLoc);
    }
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::ForStatement>> Parser::parseForStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseForStatement");

  if (!consumeExpectedToken(lexer::TokenKind::kForKeyword)) { return zc::none; }

  if (!consumeExpectedToken(lexer::TokenKind::kLeftParen)) { return zc::none; }

  // Parse init (optional)
  zc::Maybe<zc::Own<ast::Expression>> init = zc::none;
  if (!expectToken(lexer::TokenKind::kSemicolon)) { init = parseExpression(); }

  if (!consumeExpectedToken(lexer::TokenKind::kSemicolon)) { return zc::none; }

  // Parse condition (optional)
  zc::Maybe<zc::Own<ast::Expression>> condition = zc::none;
  if (!expectToken(lexer::TokenKind::kSemicolon)) { condition = parseExpression(); }

  if (!consumeExpectedToken(lexer::TokenKind::kSemicolon)) { return zc::none; }

  // Parse update (optional)
  zc::Maybe<zc::Own<ast::Expression>> update = zc::none;
  if (!expectToken(lexer::TokenKind::kRightParen)) { update = parseExpression(); }

  if (!consumeExpectedToken(lexer::TokenKind::kRightParen)) { return zc::none; }

  ZC_IF_SOME(body, parseStatement()) {
    source::SourceLoc startLoc = currentToken().getLocation();
    // Convert init expression to statement if needed
    zc::Maybe<zc::Own<ast::Statement>> initStmt = zc::none;
    ZC_IF_SOME(initExpr, init) {
      initStmt = ast::factory::createExpressionStatement(zc::mv(initExpr));
    }

    return finishNode(ast::factory::createForStatement(zc::mv(initStmt), zc::mv(condition),
                                                       zc::mv(update), zc::mv(body)),
                      startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::BreakStatement>> Parser::parseBreakStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseBreakStatement");

  source::SourceLoc startLoc = currentToken().getLocation();
  if (!consumeExpectedToken(lexer::TokenKind::kBreakKeyword)) { return zc::none; }

  // Optional label
  zc::Maybe<zc::Own<ast::Identifier>> label = zc::none;
  if (expectToken(lexer::TokenKind::kIdentifier)) { label = parseIdentifier(); }

  if (!consumeExpectedToken(lexer::TokenKind::kSemicolon)) { return zc::none; }

  return finishNode(ast::factory::createBreakStatement(zc::mv(label)), startLoc);
}

zc::Maybe<zc::Own<ast::ContinueStatement>> Parser::parseContinueStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseContinueStatement");

  source::SourceLoc startLoc = currentToken().getLocation();
  if (!consumeExpectedToken(lexer::TokenKind::kContinueKeyword)) { return zc::none; }

  // Optional label
  zc::Maybe<zc::Own<ast::Identifier>> label = zc::none;
  if (expectToken(lexer::TokenKind::kIdentifier)) { label = parseIdentifier(); }

  if (!consumeExpectedToken(lexer::TokenKind::kSemicolon)) { return zc::none; }

  return finishNode(ast::factory::createContinueStatement(zc::mv(label)), startLoc);
}

zc::Maybe<zc::Own<ast::ReturnStatement>> Parser::parseReturnStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseReturnStatement");

  source::SourceLoc startLoc = currentToken().getLocation();
  if (!consumeExpectedToken(lexer::TokenKind::kReturnKeyword)) { return zc::none; }

  // Optional expression
  zc::Maybe<zc::Own<ast::Expression>> expr = zc::none;
  if (!expectToken(lexer::TokenKind::kSemicolon)) { expr = parseExpression(); }

  if (!consumeExpectedToken(lexer::TokenKind::kSemicolon)) { return zc::none; }

  // Create return statement AST node
  return finishNode(ast::factory::createReturnStatement(zc::mv(expr)), startLoc);
}

zc::Maybe<zc::Own<ast::MatchStatement>> Parser::parseMatchStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseMatchStatement");

  if (!consumeExpectedToken(lexer::TokenKind::kMatchKeyword)) { return zc::none; }

  if (!consumeExpectedToken(lexer::TokenKind::kLeftParen)) { return zc::none; }

  ZC_IF_SOME(expr, parseExpression()) {
    if (!consumeExpectedToken(lexer::TokenKind::kRightParen)) { return zc::none; }

    if (!consumeExpectedToken(lexer::TokenKind::kLeftBrace)) { return zc::none; }

    // Parse match clauses
    zc::Vector<zc::Own<ast::Statement>> clauses;
    while (!expectToken(lexer::TokenKind::kRightBrace)) {
      // Parse match clause: pattern => statement
      ZC_IF_SOME(pattern, parseExpression()) {
        (void)pattern;  // Suppress unused variable warning
        if (consumeExpectedToken(lexer::TokenKind::kArrow)) {
          ZC_IF_SOME(statement, parseStatement()) {
            // Create match clause (simplified as statement for now)
            clauses.add(zc::mv(statement));
          }
        }
      }
    }

    if (!consumeExpectedToken(lexer::TokenKind::kRightBrace)) { return zc::none; }

    source::SourceLoc startLoc = currentToken().getLocation();
    return finishNode(ast::factory::createMatchStatement(zc::mv(expr), zc::mv(clauses)), startLoc);
  }

  return zc::none;
}

// ================================================================================
// Declaration parsing implementations

zc::Maybe<zc::Own<ast::Statement>> Parser::parseDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseDeclaration");

  // declaration:
  //   functionDeclaration
  //   | classDeclaration
  //   | interfaceDeclaration
  //   | aliasDeclaration
  //   | structDeclaration
  //   | errorDeclaration
  //   | enumDeclaration
  //   | variableDeclaration;

  const lexer::Token& token = currentToken();

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

  // variableDeclaration: LET_OR_CONST bindingList;
  // bindingList: bindingElement (COMMA bindingElement)*;
  // bindingElement: bindingIdentifier typeAnnotation? initializer?;

  lexer::TokenKind declKind = currentToken().getKind();
  if (declKind != lexer::TokenKind::kLetKeyword && declKind != lexer::TokenKind::kVarKeyword) {
    return zc::none;
  }

  source::SourceLoc startLoc = currentToken().getLocation();
  consumeToken();  // consume let/const

  // Parse bindingList: bindingElement (COMMA bindingElement)*
  zc::Vector<zc::Own<ast::BindingElement>> bindings;

  // Parse first bindingElement
  ZC_IF_SOME(firstBinding, parseBindingElement()) {
    bindings.add(zc::mv(firstBinding));

    // Parse additional bindingElements separated by commas
    while (expectToken(lexer::TokenKind::kComma)) {
      consumeToken();  // consume comma
      ZC_IF_SOME(binding, parseBindingElement()) { bindings.add(zc::mv(binding)); }
      else {
        // Error: expected bindingElement after comma
        return zc::none;
      }
    }

    // Create variable declaration AST node
    return finishNode(ast::factory::createVariableDeclaration(zc::mv(bindings)), startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::FunctionDeclaration>> Parser::parseFunctionDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseFunctionDeclaration");

  // functionDeclaration:
  //   FUN bindingIdentifier callSignature LBRACE functionBody RBRACE;

  source::SourceLoc startLoc = currentToken().getLocation();
  if (!consumeExpectedToken(lexer::TokenKind::kFunKeyword)) { return zc::none; }

  ZC_IF_SOME(name, parseBindingIdentifier()) {
    // Parse function signature (type parameters, parameters and return type)
    zc::Vector<zc::Own<ast::TypeParameter>> typeParameters = parseTypeParameters();
    zc::Vector<zc::Own<ast::BindingElement>> parameters = parseParameters();
    zc::Maybe<zc::Own<ast::ReturnType>> returnType = parseReturnType();

    // Parse function body
    ZC_IF_SOME(body, parseBlockStatement()) {
      // Create function declaration AST node
      return finishNode(ast::factory::createFunctionDeclaration(
                            zc::mv(name), zc::mv(typeParameters), zc::mv(parameters),
                            zc::mv(returnType), zc::mv(body)),
                        startLoc);
    }
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::ClassDeclaration>> Parser::parseClassDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseClassDeclaration");

  source::SourceLoc startLoc = currentToken().getLocation();
  if (!consumeExpectedToken(lexer::TokenKind::kClassKeyword)) { return zc::none; }

  ZC_IF_SOME(name, parseBindingIdentifier()) {
    // Optional extends clause
    if (expectToken(lexer::TokenKind::kExtendsKeyword)) {
      consumeToken();
      // TODO: Parse superclass
      parseBindingIdentifier();
    }

    // Parse class body
    if (!consumeExpectedToken(lexer::TokenKind::kLeftBrace)) { return zc::none; }

    zc::Vector<zc::Own<ast::Statement>> bodyStatements;

    // Parse class members
    while (!expectToken(lexer::TokenKind::kRightBrace) && !expectToken(lexer::TokenKind::kEOF)) {
      ZC_IF_SOME(member, parseStatement()) { bodyStatements.add(zc::mv(member)); }
      else {
        // Skip invalid tokens
        consumeToken();
      }
    }

    if (!consumeExpectedToken(lexer::TokenKind::kRightBrace)) { return zc::none; }

    // Create class declaration AST node
    return finishNode(ast::factory::createClassDeclaration(zc::mv(name), zc::mv(bodyStatements)),
                      startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::InterfaceDeclaration>> Parser::parseInterfaceDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseInterfaceDeclaration");

  if (!consumeExpectedToken(lexer::TokenKind::kInterfaceKeyword)) { return zc::none; }

  ZC_IF_SOME(name, parseBindingIdentifier()) {
    source::SourceLoc startLoc = currentToken().getLocation();

    // Parse interface body
    if (!consumeExpectedToken(lexer::TokenKind::kLeftBrace)) { return zc::none; }

    zc::Vector<zc::Own<ast::Statement>> members;
    while (!expectToken(lexer::TokenKind::kRightBrace)) {
      // Parse interface members (simplified)
      ZC_IF_SOME(member, parseStatement()) { members.add(zc::mv(member)); }
    }

    if (!consumeExpectedToken(lexer::TokenKind::kRightBrace)) { return zc::none; }

    return finishNode(ast::factory::createInterfaceDeclaration(zc::mv(name), zc::mv(members)),
                      startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::StructDeclaration>> Parser::parseStructDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseStructDeclaration");

  if (!consumeExpectedToken(lexer::TokenKind::kStructKeyword)) { return zc::none; }

  ZC_IF_SOME(name, parseBindingIdentifier()) {
    source::SourceLoc startLoc = currentToken().getLocation();

    // Parse struct body
    if (!consumeExpectedToken(lexer::TokenKind::kLeftBrace)) { return zc::none; }

    zc::Vector<zc::Own<ast::Statement>> fields;
    while (!expectToken(lexer::TokenKind::kRightBrace)) {
      // Parse struct fields (simplified as statements)
      ZC_IF_SOME(field, parseStatement()) { fields.add(zc::mv(field)); }
    }

    if (!consumeExpectedToken(lexer::TokenKind::kRightBrace)) { return zc::none; }

    return finishNode(ast::factory::createStructDeclaration(zc::mv(name), zc::mv(fields)),
                      startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::EnumDeclaration>> Parser::parseEnumDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseEnumDeclaration");

  if (!consumeExpectedToken(lexer::TokenKind::kEnumKeyword)) { return zc::none; }

  ZC_IF_SOME(name, parseBindingIdentifier()) {
    source::SourceLoc startLoc = currentToken().getLocation();

    // Parse enum body
    if (!consumeExpectedToken(lexer::TokenKind::kLeftBrace)) { return zc::none; }

    zc::Vector<zc::Own<ast::Statement>> members;
    while (!expectToken(lexer::TokenKind::kRightBrace)) {
      // Parse enum members (simplified as statements)
      ZC_IF_SOME(member, parseStatement()) { members.add(zc::mv(member)); }
      // Optional comma
      if (expectToken(lexer::TokenKind::kComma)) { consumeToken(); }
    }

    if (!consumeExpectedToken(lexer::TokenKind::kRightBrace)) { return zc::none; }

    return finishNode(ast::factory::createEnumDeclaration(zc::mv(name), zc::mv(members)), startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::ErrorDeclaration>> Parser::parseErrorDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseErrorDeclaration");

  if (!consumeExpectedToken(lexer::TokenKind::kErrorKeyword)) { return zc::none; }

  ZC_IF_SOME(name, parseBindingIdentifier()) {
    source::SourceLoc startLoc = currentToken().getLocation();

    // Parse error body (optional)
    zc::Vector<zc::Own<ast::Statement>> fields;
    if (expectToken(lexer::TokenKind::kLeftBrace)) {
      consumeToken();

      while (!expectToken(lexer::TokenKind::kRightBrace)) {
        // Parse error fields (simplified as statements)
        ZC_IF_SOME(field, parseStatement()) { fields.add(zc::mv(field)); }
      }

      if (!consumeExpectedToken(lexer::TokenKind::kRightBrace)) { return zc::none; }
    }

    return finishNode(ast::factory::createErrorDeclaration(zc::mv(name), zc::mv(fields)), startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::AliasDeclaration>> Parser::parseAliasDeclaration() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseAliasDeclaration");

  source::SourceLoc startLoc = currentToken().getLocation();
  if (!consumeExpectedToken(lexer::TokenKind::kAliasKeyword)) { return zc::none; }

  ZC_IF_SOME(name, parseBindingIdentifier()) {
    if (!consumeExpectedToken(lexer::TokenKind::kEquals)) { return zc::none; }

    ZC_IF_SOME(type, parseType()) {
      if (!consumeExpectedToken(lexer::TokenKind::kSemicolon)) { return zc::none; }
      source::SourceLoc endLoc = currentToken().getLocation();

      return finishNode(ast::factory::createAliasDeclaration(zc::mv(name), zc::mv(type)), startLoc,
                        endLoc);
    }
  }

  return zc::none;
}

// ================================================================================
// Expression parsing implementations

namespace {

// Get AST binary operator precedence directly from token
ast::OperatorPrecedence getBinaryOperatorPrecedence(lexer::TokenKind tokenKind) {
  switch (tokenKind) {
    case lexer::TokenKind::kBarBar:
      return ast::OperatorPrecedence::kLogicalOr;
    case lexer::TokenKind::kAmpersandAmpersand:
      return ast::OperatorPrecedence::kLogicalAnd;
    case lexer::TokenKind::kBar:
      return ast::OperatorPrecedence::kBitwiseOr;
    case lexer::TokenKind::kCaret:
      return ast::OperatorPrecedence::kBitwiseXor;
    case lexer::TokenKind::kAmpersand:
      return ast::OperatorPrecedence::kBitwiseAnd;
    case lexer::TokenKind::kEqualsEquals:
    case lexer::TokenKind::kExclamationEquals:
      return ast::OperatorPrecedence::kEquality;
    case lexer::TokenKind::kLessThan:
    case lexer::TokenKind::kGreaterThan:
    case lexer::TokenKind::kLessThanEquals:
    case lexer::TokenKind::kGreaterThanEquals:
      return ast::OperatorPrecedence::kRelational;
    case lexer::TokenKind::kLessThanLessThan:
    case lexer::TokenKind::kGreaterThanGreaterThan:
      return ast::OperatorPrecedence::kShift;
    case lexer::TokenKind::kPlus:
    case lexer::TokenKind::kMinus:
      return ast::OperatorPrecedence::kAdditive;
    case lexer::TokenKind::kAsterisk:
    case lexer::TokenKind::kSlash:
    case lexer::TokenKind::kPercent:
      return ast::OperatorPrecedence::kMultiplicative;
    case lexer::TokenKind::kAsteriskAsterisk:
      return ast::OperatorPrecedence::kExponentiation;
    default:
      return ast::OperatorPrecedence::kLowest;
  }
}

// Check if token is a binary operator
bool isBinaryOperator(lexer::TokenKind tokenKind) {
  return getBinaryOperatorPrecedence(tokenKind) > ast::OperatorPrecedence::kLowest;
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
  //
  // Parses a comma-separated list of assignment expressions

  ZC_IF_SOME(assignExpr, parseAssignmentExpressionOrHigher()) {
    zc::Own<ast::Expression> expr = zc::mv(assignExpr);
    // Handle comma operator
    while (expectToken(lexer::TokenKind::kComma)) {
      consumeToken();
      ZC_IF_SOME(rightAssign, parseAssignmentExpressionOrHigher()) {
        zc::Own<ast::Expression> right = zc::mv(rightAssign);
        // Create comma expression AST node
        zc::Own<ast::BinaryOperator> op =
            ast::factory::createBinaryOperator(zc::str(","), ast::OperatorPrecedence::kLowest);
        zc::Own<ast::Expression> newExpr =
            ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(right));
        // TODO: Set source range for binary expression
        expr = zc::mv(newExpr);
      }
    }
    return zc::mv(expr);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::Expression>> Parser::parseInitializer() {
  if (consumeExpectedToken(lexer::TokenKind::kEquals)) {
    return parseAssignmentExpressionOrHigher();
  }
  return zc::none;
}

// Assignment expression parsing
zc::Maybe<zc::Own<ast::Expression>> Parser::parseAssignmentExpressionOrHigher() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser,
                                 "parseAssignmentExpressionOrHigher");

  // assignmentExpression:
  //   conditionalExpression
  //   | functionExpression
  //   | leftHandSideExpression ASSIGN assignmentExpression
  //   | leftHandSideExpression assignmentOperator assignmentExpression
  //   | leftHandSideExpression AND_ASSIGN assignmentExpression
  //   | leftHandSideExpression OR_ASSIGN assignmentExpression
  //   | leftHandSideExpression NULL_COALESCE_ASSIGN assignmentExpression;
  //
  // Try to parse function expression first, then binary expression

  // First try to parse function expression
  ZC_IF_SOME(funcExpr, parseFunctionExpression()) { return zc::mv(funcExpr); }

  // Parse binary expression with lowest precedence to get the left operand
  ZC_IF_SOME(expr, parseBinaryExpressionOrHigher(ast::OperatorPrecedence::kLowest)) {
    const lexer::Token& token = currentToken();

    // Check for assignment operators - if found and expr is left-hand side, parse assignment
    if (isAssignmentOperator(token.getKind()) && isLeftHandSideExpression(*expr)) {
      zc::String opText = token.getText(impl->sourceMgr);
      consumeToken();

      // Right-associative: recursively parse assignment expression
      ZC_IF_SOME(right, parseAssignmentExpressionOrHigher()) {
        auto op = ast::factory::createAssignmentOperator(zc::mv(opText));
        auto assignExpr =
            ast::factory::createAssignmentExpression(zc::mv(expr), zc::mv(op), zc::mv(right));
        // TODO: Set source range for assignment expression
        return assignExpr;
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

  consumeToken();  // consume '?'

  // Parse the 'then' expression
  ZC_IF_SOME(thenExpr, parseAssignmentExpressionOrHigher()) {
    // Expect ':' token
    if (!consumeExpectedToken(lexer::TokenKind::kColon)) { return zc::none; }

    // Parse the 'else' expression
    ZC_IF_SOME(elseExpr, parseAssignmentExpressionOrHigher()) {
      // Create conditional expression AST node
      auto conditionalExpr = ast::factory::createConditionalExpression(
          zc::mv(leftOperand), zc::mv(thenExpr), zc::mv(elseExpr));
      // TODO: Set source range for conditional expression
      return conditionalExpr;
    }
  }

  return zc::none;
}

// Binary expression parsing
zc::Maybe<zc::Own<ast::Expression>> Parser::parseBinaryExpressionOrHigher(
    ast::OperatorPrecedence precedence) {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseBinaryExpressionOrHigher");

  // Handles all binary expressions with precedence:
  //   bitwiseORExpression | bitwiseXORExpression | bitwiseANDExpression
  //   | equalityExpression | relationalExpression | shiftExpression
  //   | additiveExpression | multiplicativeExpression | exponentiationExpression
  //
  // Uses operator precedence parsing for left-to-right associativity

  const source::SourceLoc startLoc = currentToken().getLocation();
  // Parse the left operand (unary expression or higher)
  ZC_IF_SOME(leftOperand, parseUnaryExpressionOrHigher()) {
    // Parse the rest of the binary expression with lowest precedence
    return parseBinaryExpressionRest(zc::mv(leftOperand), precedence, startLoc);
  }

  return zc::none;
}

// Binary expression rest parsing
zc::Maybe<zc::Own<ast::Expression>> Parser::parseBinaryExpressionRest(
    zc::Own<ast::Expression> leftOperand, ast::OperatorPrecedence precedence,
    source::SourceLoc startLoc) {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseBinaryExpressionRest");

  zc::Own<ast::Expression> expr = zc::mv(leftOperand);

  while (true) {
    const lexer::Token& token = currentToken();

    if (!isBinaryOperator(token.getKind())) { break; }

    ast::OperatorPrecedence newPrecedence = getBinaryOperatorPrecedence(token.getKind());

    // Check the precedence to see if we should "take" this operator
    // - For left associative operator (all operator but **), consume the operator,
    //   recursively call the function below, and parse binaryExpression as a rightOperand
    //   of the caller if the new precedence of the operator is strictly greater than the current
    //   precedence.
    // - For right associative operator (**), consume the operator, recursively call the function
    //   and parse binaryExpression as a rightOperand of the caller if the new precedence of
    //   the operator is greater than or equal to the current precedence
    bool isRightAssociative = token.is(lexer::TokenKind::kAsteriskAsterisk);
    bool consumeCurrentOperator =
        isRightAssociative ? newPrecedence >= precedence : newPrecedence > precedence;

    if (!consumeCurrentOperator) { break; }

    if (expectToken(lexer::TokenKind::kAsKeyword)) {
      // Make sure we *do* perform ASI for constructs like this:
      //    let x = foo
      //    as (Bar)
      // This should be parsed as an initialized variable, followed
      // by a function call to 'as' with the argument 'Bar'
      if (currentToken().hasPrecedingLineBreak()) {
        // 'as' keyword must be on the same line as the variable declaration
        break;
      }

      ZC_IF_SOME(castExpr, parseCastExpression()) { expr = zc::mv(castExpr); }
      else {
        // 'as', 'as!', 'as?' parse failed
        break;
      }
    } else {
      zc::String opText = token.getText(impl->sourceMgr);
      consumeToken();

      ZC_IF_SOME(rightOperand, parseBinaryExpressionOrHigher(newPrecedence)) {
        // Convert parser precedence to AST precedence
        auto astPrecedence = getBinaryOperatorPrecedence(currentToken().getKind());
        // Create binary operator from current token
        auto op = ast::factory::createBinaryOperator(zc::mv(opText), astPrecedence);
        expr = finishNode(
            ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(rightOperand)),
            startLoc);
      }
    }
  }

  return zc::mv(expr);
}

// Unary expression parsing
zc::Maybe<zc::Own<ast::Expression>> Parser::parseUnaryExpressionOrHigher() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseUnaryExpressionOrHigher");

  // postfixUnaryExpression:
  //   leftHandSideExpression (
  //     ERROR_PROPAGATE
  //     | FORCE_UNWRAP
  //     | INC
  //     | DEC
  //   )*;
  //
  // prefixUnaryExpression:
  //   postfixUnaryExpression
  //   | VOID prefixUnaryExpression
  //   | TYPEOF prefixUnaryExpression
  //   | PLUS prefixUnaryExpression
  //   | MINUS prefixUnaryExpression
  //   | BIT_NOT prefixUnaryExpression
  //   | NOT prefixUnaryExpression
  //   | INC prefixUnaryExpression
  //   | DEC prefixUnaryExpression
  //   | AWAIT prefixUnaryExpression;
  //
  // Handles prefix unary operators and update expressions

  const lexer::Token& token = currentToken();

  if (isUpdateExpression(token.getKind())) {
    const source::SourceLoc startLoc = token.getLocation();
    ZC_IF_SOME(updateExpr, parseUpdateExpression()) {
      return expectToken(lexer::TokenKind::kAsteriskAsterisk)
                 ? parseBinaryExpressionRest(zc::mv(updateExpr),
                                             getBinaryOperatorPrecedence(token.getKind()), startLoc)
                 : finishNode(zc::mv(updateExpr), startLoc);
    }
  }

  const lexer::Token unaryOperator = currentToken();
  const source::SourceLoc startLoc = unaryOperator.getLocation();
  ZC_IF_SOME(simpleUnaryExpression, parseSimpleUnaryExpression()) {
    // Check if followed by exponentiation operator
    if (expectToken(lexer::TokenKind::kAsteriskAsterisk)) {
      // This is a unary expression with operator
      zc::String operatorText = unaryOperator.getText(impl->sourceMgr);
      impl->diagnosticEngine.diagnose<diagnostics::DiagID::UnaryExpressionInExponentiation>(
          startLoc, zc::mv(operatorText));
    }
    return zc::mv(simpleUnaryExpression);
  }

  return zc::none;
}

// Simple unary expression parsing
zc::Maybe<zc::Own<ast::UnaryExpression>> Parser::parseSimpleUnaryExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseSimpleUnaryExpression");

  switch (currentToken().getKind()) {
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

  zc::String operatorText = currentToken().getText(impl->sourceMgr);
  consumeToken();

  // Parse the operand (recursive call to parseSimpleUnaryExpression)
  ZC_IF_SOME(operand, parseSimpleUnaryExpression()) {
    // Create appropriate unary operator based on token kind
    zc::Own<ast::UnaryOperator> op;
    switch (currentToken().getKind()) {
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
    auto prefixExpr = ast::factory::createPrefixUnaryExpression(zc::mv(op), zc::mv(operand));
    // TODO: Set source range for prefix unary expression
    return prefixExpr;
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

  zc::String operatorText = currentToken().getText(impl->sourceMgr);
  consumeToken();

  // Parse the operand
  ZC_IF_SOME(operand, parseSimpleUnaryExpression()) {
    // Create void expression
    auto voidExpr = ast::factory::createVoidExpression(zc::mv(operand));
    // TODO: Set source range for void expression
    return voidExpr;
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

  zc::String operatorText = currentToken().getText(impl->sourceMgr);
  consumeToken();

  // Parse the operand
  ZC_IF_SOME(operand, parseSimpleUnaryExpression()) {
    // Create typeof expression
    auto typeofExpr = ast::factory::createTypeOfExpression(zc::mv(operand));
    // TODO: Set source range for typeof expression
    return typeofExpr;
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

  zc::Maybe<zc::Own<ast::MemberExpression>> expression = zc::none;

  // Handle super keyword
  if (expectToken(lexer::TokenKind::kSuperKeyword)) {
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
  if (expectToken(lexer::TokenKind::kNewKeyword)) {
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
    // Handle function calls
    if (expectToken(lexer::TokenKind::kLeftParen)) {
      // Parse argument list
      ZC_IF_SOME(arguments, parseArgumentList()) {
        // Create call expression
        auto callExpr = ast::factory::createCallExpression(zc::mv(result), zc::mv(arguments));
        // TODO: Set source range for call expression
        result = zc::mv(callExpr);
        continue;
      }
      else { return zc::none; }
    }
    // Handle member access (.property)
    else if (expectToken(lexer::TokenKind::kPeriod)) {
      consumeToken();  // consume '.'
      ZC_IF_SOME(name, parseIdentifier()) {
        auto propAccessExpr =
            ast::factory::createPropertyAccessExpression(zc::mv(result), zc::mv(name), false);
        // TODO: Set source range for property access expression
        result = zc::mv(propAccessExpr);
        continue;
      }
    }
    // Handle computed member access ([expression])
    else if (expectToken(lexer::TokenKind::kLeftBracket)) {
      consumeToken();  // consume '['
      ZC_IF_SOME(index, parseExpression()) {
        if (!consumeExpectedToken(lexer::TokenKind::kRightBracket)) { return zc::none; }
        auto elemAccessExpr =
            ast::factory::createElementAccessExpression(zc::mv(result), zc::mv(index), false);
        // TODO: Set source range for element access expression
        result = zc::mv(elemAccessExpr);
        continue;
      }
    }
    // Handle optional chaining (?.) - simplified for now
    else if (expectToken(lexer::TokenKind::kQuestion)) {
      consumeToken();  // consume '?'
      if (expectToken(lexer::TokenKind::kPeriod)) {
        consumeToken();  // consume '.'

        ZC_IF_SOME(property, parseIdentifier()) {
          // Create optional expression directly
          auto optionalExpr =
              ast::factory::createOptionalExpression(zc::mv(result), zc::mv(property));
          // TODO: Set source range for optional expression
          result = zc::mv(optionalExpr);
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
      consumeToken();

      ZC_IF_SOME(thenExpr, parseAssignmentExpressionOrHigher()) {
        if (!consumeExpectedToken(lexer::TokenKind::kColon)) { return zc::none; }

        ZC_IF_SOME(elseExpr, parseAssignmentExpressionOrHigher()) {
          auto conditionalExpr = ast::factory::createConditionalExpression(
              zc::mv(expr), zc::mv(thenExpr), zc::mv(elseExpr));
          // TODO: Set source range for conditional expression
          return conditionalExpr;
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
      zc::String opText = currentToken().getText(impl->sourceMgr);
      consumeToken();

      ZC_IF_SOME(right, parseLogicalAndExpression()) {
        // Create binary expression AST node
        auto op =
            ast::factory::createBinaryOperator(zc::mv(opText), ast::OperatorPrecedence::kLogicalOr);
        auto newExpr =
            ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(right));
        // TODO: Set source range for binary expression
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
      zc::String opText = currentToken().getText(impl->sourceMgr);
      consumeToken();

      ZC_IF_SOME(right, parseBitwiseOrExpression()) {
        // Create binary expression AST node
        auto op = ast::factory::createBinaryOperator(zc::mv(opText),
                                                     ast::OperatorPrecedence::kLogicalAnd);
        auto newExpr =
            ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(right));
        // TODO: Set source range for binary expression
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
      zc::String opText = currentToken().getText(impl->sourceMgr);
      consumeToken();

      ZC_IF_SOME(right, parseBitwiseXorExpression()) {
        // Create binary expression AST node
        auto op =
            ast::factory::createBinaryOperator(zc::mv(opText), ast::OperatorPrecedence::kBitwiseOr);
        auto newExpr =
            ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(right));
        // TODO: Set source range for binary expression
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
      zc::String opText = currentToken().getText(impl->sourceMgr);
      consumeToken();

      ZC_IF_SOME(right, parseBitwiseAndExpression()) {
        // Create binary expression AST node
        auto op = ast::factory::createBinaryOperator(zc::mv(opText),
                                                     ast::OperatorPrecedence::kBitwiseXor);
        auto newExpr =
            ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(right));
        // TODO: Set source range for binary expression
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
      zc::String opText = currentToken().getText(impl->sourceMgr);
      consumeToken();

      ZC_IF_SOME(right, parseEqualityExpression()) {
        // Create binary expression AST node
        auto op = ast::factory::createBinaryOperator(zc::mv(opText),
                                                     ast::OperatorPrecedence::kBitwiseAnd);
        auto newExpr =
            ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(right));
        // TODO: Set source range for binary expression
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
      zc::String opText = currentToken().getText(impl->sourceMgr);
      consumeToken();

      ZC_IF_SOME(right, parseRelationalExpression()) {
        // Create binary expression AST node
        auto op =
            ast::factory::createBinaryOperator(zc::mv(opText), ast::OperatorPrecedence::kEquality);
        auto newExpr =
            ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(right));
        // TODO: Set source range for binary expression
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
      zc::String opText = currentToken().getText(impl->sourceMgr);
      consumeToken();

      ZC_IF_SOME(right, parseShiftExpression()) {
        // Create binary expression AST node
        auto op = ast::factory::createBinaryOperator(zc::mv(opText),
                                                     ast::OperatorPrecedence::kRelational);
        auto newExpr =
            ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(right));
        // TODO: Set source range for binary expression
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
      zc::String opText = currentToken().getText(impl->sourceMgr);
      consumeToken();

      ZC_IF_SOME(right, parseAdditiveExpression()) {
        // Create binary expression AST node
        auto op =
            ast::factory::createBinaryOperator(zc::mv(opText), ast::OperatorPrecedence::kShift);
        auto newExpr =
            ast::factory::createBinaryExpression(zc::mv(expr), zc::mv(op), zc::mv(right));
        // TODO: Set source range for binary expression
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
      zc::String opText = currentToken().getText(impl->sourceMgr);
      consumeToken();

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
      zc::String opText = currentToken().getText(impl->sourceMgr);
      consumeToken();

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
    const lexer::Token token = currentToken();
    if (token.is(lexer::TokenKind::kAsteriskAsterisk)) {  // POW operator
      zc::String opText = token.getText(impl->sourceMgr);
      consumeToken();

      ZC_IF_SOME(right, parseExponentiationExpression()) {  // Right-associative
        auto op = ast::factory::createBinaryOperator(zc::mv(opText),
                                                     ast::OperatorPrecedence::kExponentiation,
                                                     ast::OperatorAssociativity::kRight);
        auto binaryExpr =
            ast::factory::createBinaryExpression(zc::mv(left), zc::mv(op), zc::mv(right));
        // TODO: Set source range for binary expression
        return binaryExpr;
      }
    }
    return zc::mv(left);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::Expression>> Parser::parseUnaryExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseUnaryExpression");

  // unaryExpression:
  //   updateExpression
  //   | VOID unaryExpression
  //   | TYPEOF unaryExpression
  //   | PLUS unaryExpression
  //   | MINUS unaryExpression
  //   | BIT_NOT unaryExpression
  //   | NOT unaryExpression
  //   | awaitExpression
  //
  // Handles prefix unary operators and type assertions

  const lexer::Token& token = currentToken();

  // Check for VOID operator
  if (token.is(lexer::TokenKind::kVoidKeyword)) {
    zc::String opText = token.getText(impl->sourceMgr);
    consumeToken();

    ZC_IF_SOME(expr, parseUnaryExpression()) {
      auto op = ast::factory::createVoidOperator();
      auto unaryExpr = ast::factory::createPrefixUnaryExpression(zc::mv(op), zc::mv(expr));
      // TODO: Set source range for prefix unary expression
      return zc::mv(unaryExpr);
    }
  }
  // Check for TYPEOF operator
  else if (token.is(lexer::TokenKind::kTypeOfKeyword)) {
    zc::String opText = token.getText(impl->sourceMgr);
    consumeToken();

    ZC_IF_SOME(expr, parseUnaryExpression()) {
      auto op = ast::factory::createTypeOfOperator();
      auto unaryExpr = ast::factory::createPrefixUnaryExpression(zc::mv(op), zc::mv(expr));
      // TODO: Set source range for prefix unary expression
      return zc::mv(unaryExpr);
    }
  }
  // Check for basic unary operators: +, -, !, ~
  else if (token.is(lexer::TokenKind::kPlus) || token.is(lexer::TokenKind::kMinus) ||
           token.is(lexer::TokenKind::kExclamation) || token.is(lexer::TokenKind::kTilde)) {
    zc::String opText = token.getText(impl->sourceMgr);
    consumeToken();

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
      // TODO: Set source range for prefix unary expression
      return zc::mv(unaryExpr);
    }
  }
  // Check for AWAIT expression
  else if (token.is(lexer::TokenKind::kAwaitKeyword)) {
    // Delegate to parseAwaitExpression, but we need to return a UnaryExpression
    // For now, treat await as a unary operator
    zc::String opText = token.getText(impl->sourceMgr);
    consumeToken();

    ZC_IF_SOME(expr, parseUnaryExpression()) {
      // For await, we should use AwaitExpression instead of UnaryExpression
      // But for now, treat it as a unary operator
      auto op = ast::factory::createUnaryOperator(zc::str("await"), true);
      auto unaryExpr = ast::factory::createPrefixUnaryExpression(zc::mv(op), zc::mv(expr));
      // TODO: Set source range for prefix unary expression
      return zc::mv(unaryExpr);
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

  const lexer::Token token = currentToken();
  const source::SourceLoc startLoc = token.getLocation();

  // Check for prefix increment/decrement operators
  if (token.is(lexer::TokenKind::kPlusPlus) || token.is(lexer::TokenKind::kMinusMinus)) {
    zc::String opText = token.getText(impl->sourceMgr);
    consumeToken();

    // For prefix operators, parse unaryExpression (not leftHandSideExpression)
    ZC_IF_SOME(operand, parseLeftHandSideExpressionOrHigher()) {
      zc::Own<ast::UnaryOperator> op = token.is(lexer::TokenKind::kPlusPlus)
                                           ? ast::factory::createPreIncrementOperator()
                                           : ast::factory::createPreDecrementOperator();
      auto prefixExpr = ast::factory::createPrefixUnaryExpression(zc::mv(op), zc::mv(operand));
      // TODO: Set source range for prefix unary expression
      return prefixExpr;
    }

    return zc::none;
  }

  // Parse leftHandSideExpression first
  ZC_IF_SOME(expression, parseLeftHandSideExpressionOrHigher()) {
    const lexer::Token postToken = currentToken();

    // Check for postfix increment/decrement operators
    if ((postToken.is(lexer::TokenKind::kPlusPlus) ||
         postToken.is(lexer::TokenKind::kMinusMinus))) {
      zc::String opText = postToken.getText(impl->sourceMgr);
      consumeToken();

      zc::Own<ast::UnaryOperator> op = postToken.is(lexer::TokenKind::kPlusPlus)
                                           ? ast::factory::createPostIncrementOperator()
                                           : ast::factory::createPostDecrementOperator();
      return finishNode(ast::factory::createPostfixUnaryExpression(zc::mv(op), zc::mv(expression)),
                        startLoc);
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

  switch (currentToken().getKind()) {
    case lexer::TokenKind::kIdentifier:
      return parseIdentifier();

    case lexer::TokenKind::kIntegerLiteral:
    case lexer::TokenKind::kFloatLiteral:
    case lexer::TokenKind::kStringLiteral:
    case lexer::TokenKind::kTrueKeyword:
    case lexer::TokenKind::kFalseKeyword:
    case lexer::TokenKind::kNullKeyword:
      return parseLiteralExpression();

    case lexer::TokenKind::kLeftParen: {
      ZC_IF_SOME(expr, parseParenthesizedExpression()) { return zc::mv(expr); }
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
  //   nullLiteral
  //   | booleanLiteral
  //   | numericLiteral
  //   | stringLiteral;
  //
  // Handles all literal values (numbers, strings, booleans, null)

  const lexer::Token& token = currentToken();
  source::SourceLoc startLoc = token.getLocation();

  switch (token.getKind()) {
    case lexer::TokenKind::kIntegerLiteral: {
      zc::String value = token.getText(impl->sourceMgr);
      consumeToken();
      int64_t numValue = value.parseAs<int64_t>();
      return finishNode(ast::factory::createIntegerLiteral(numValue), startLoc);
    }
    case lexer::TokenKind::kFloatLiteral: {
      zc::String value = token.getText(impl->sourceMgr);
      consumeToken();
      double numValue = zc::StringPtr(value).parseAs<double>();
      return finishNode(ast::factory::createFloatLiteral(numValue), startLoc);
    }
    case lexer::TokenKind::kStringLiteral: {
      zc::String value = token.getText(impl->sourceMgr);
      consumeToken();
      return finishNode(ast::factory::createStringLiteral(zc::mv(value)), startLoc);
    }
    case lexer::TokenKind::kTrueKeyword: {
      consumeToken();
      return finishNode(ast::factory::createBooleanLiteral(true), startLoc);
    }
    case lexer::TokenKind::kFalseKeyword: {
      consumeToken();
      return finishNode(ast::factory::createBooleanLiteral(false), startLoc);
    }
    case lexer::TokenKind::kNullKeyword: {
      consumeToken();
      return finishNode(ast::factory::createNullLiteral(), startLoc);
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

  if (!consumeExpectedToken(lexer::TokenKind::kLeftBracket)) { return zc::none; }

  zc::Vector<zc::Own<ast::Expression>> elements;

  if (!expectToken(lexer::TokenKind::kRightBracket)) {
    do {
      ZC_IF_SOME(element, parseAssignmentExpressionOrHigher()) { elements.add(zc::mv(element)); }
    } while (consumeExpectedToken(lexer::TokenKind::kComma));
  }

  if (!consumeExpectedToken(lexer::TokenKind::kRightBracket)) { return zc::none; }

  return ast::factory::createArrayLiteralExpression(zc::mv(elements));
}

zc::Maybe<zc::Own<ast::ObjectLiteralExpression>> Parser::parseObjectLiteralExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseObjectLiteralExpression");

  // objectLiteral:
  //   LBRACE RBRACE
  //   | LBRACE propertyDefinitionList RBRACE
  //   | LBRACE propertyDefinitionList COMMA RBRACE;
  //
  // Handles object literal expressions {key: value}

  if (!consumeExpectedToken(lexer::TokenKind::kLeftBrace)) { return zc::none; }

  zc::Vector<zc::Own<ast::Expression>> properties;

  // Parse object properties if not empty
  if (!expectToken(lexer::TokenKind::kRightBrace)) {
    do {
      // For now, parse simple property assignments
      ZC_IF_SOME(property, parseAssignmentExpressionOrHigher()) {
        properties.add(zc::mv(property));
      }
    } while (consumeExpectedToken(lexer::TokenKind::kComma));
  }

  if (!consumeExpectedToken(lexer::TokenKind::kRightBrace)) { return zc::none; }

  return ast::factory::createObjectLiteralExpression(zc::mv(properties));
}

// ================================================================================
// Type parsing implementations

zc::Maybe<zc::Own<ast::Type>> Parser::parseType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseType");

  // type: unionType;
  //
  // unionType: intersectionType (BIT_OR intersectionType)*;
  //
  // intersectionType: postfixType (BIT_AND postfixType)*;
  //
  // postfixType: typeAtom (LBRACK RBRACK | QUESTION)*;
  //
  // typeAtom:
  //   parenthesizedType
  //   | predefinedType
  //   | typeReference
  //   | objectType
  //   | tupleType
  //   | typeQuery;
  //
  // parenthesizedType: LPAREN type RPAREN;
  //
  // predefinedType:
  //   I8
  //   | I32
  //   | I64
  //   | U8
  //   | U16
  //   | U32
  //   | U64
  //   | F32
  //   | F64
  //   | STR
  //   | BOOL
  //   | NIL
  //   | UNIT;
  //
  // Return the parsed `unionType` directly, the suffix processing is completed in
  // `parsePrimaryType`.
  ZC_IF_SOME(type, parseUnionType()) { return zc::mv(type); }

  return zc::none;
}

zc::Maybe<zc::Own<ast::Type>> Parser::parseTypeAnnotation() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseTypeAnnotation");

  if (!consumeExpectedToken(lexer::TokenKind::kColon)) { return zc::none; }

  ZC_IF_SOME(type, parseType()) { return zc::mv(type); }

  return zc::none;
}

zc::Maybe<zc::Own<ast::Type>> Parser::parseUnionType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseUnionType");

  // unionType:
  //   intersectionType (PIPE intersectionType)*;
  //
  // Handles union types like A | B | C

  const source::SourceLoc startLoc = currentToken().getLocation();

  ZC_IF_SOME(type, parseIntersectionType()) {
    zc::Vector<zc::Own<ast::Type>> types;
    types.add(zc::mv(type));

    while (expectToken(lexer::TokenKind::kBar)) {
      consumeToken();
      ZC_IF_SOME(rightType, parseIntersectionType()) { types.add(zc::mv(rightType)); }
    }

    // Only create UnionType if there are multiple types
    if (types.size() == 1) { return zc::mv(types[0]); }

    return finishNode(ast::factory::createUnionType(zc::mv(types)), startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::Type>> Parser::parseIntersectionType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseIntersectionType");

  // intersectionType:
  //   primaryType (AMPERSAND primaryType)*;
  //
  // Handles intersection types like A & B & C

  const source::SourceLoc startLoc = currentToken().getLocation();

  ZC_IF_SOME(type, parsePostfixType()) {
    zc::Vector<zc::Own<ast::Type>> types;
    types.add(zc::mv(type));

    while (expectToken(lexer::TokenKind::kAmpersand)) {
      consumeToken();
      ZC_IF_SOME(rightType, parsePostfixType()) { types.add(zc::mv(rightType)); }
    }

    // Only create IntersectionType if there are multiple types
    if (types.size() == 1) { return zc::mv(types[0]); }

    return finishNode(ast::factory::createIntersectionType(zc::mv(types)), startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::Type>> Parser::parsePostfixType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parsePostfixType");

  // Parse postfix type according to the grammar rule:
  //
  // postfixType: typeAtom (LBRACK RBRACK | QUESTION)*
  //
  // This handles type atoms followed by optional array brackets [] or optional marker ?
  // Examples: T[], T?, T[]?, MyType[][]

  const lexer::Token token = currentToken();
  const source::SourceLoc startLoc = token.getLocation();

  // Parse the base type atom
  zc::Maybe<zc::Own<ast::Type>> maybeBase;
  switch (currentToken().getKind()) {
    case lexer::TokenKind::kLeftParen:
      // Parenthesized type: (T)
      maybeBase = parseParenthesizedType();
      break;
    case lexer::TokenKind::kLeftBrace:
      // Object type: {prop: T}
      maybeBase = parseObjectType();
      break;
    case lexer::TokenKind::kIdentifier:
      // Type reference: MyType or MyType<T>
      maybeBase = parseTypeReference();
      break;
    case lexer::TokenKind::kTypeOfKeyword:
      // Type query: typeof expr
      maybeBase = parseTypeQuery();
      break;
    default:
      // Predefined type: i32, str, bool, etc.
      maybeBase = parsePredefinedType();
      break;
  }

  ZC_IF_SOME(result, zc::mv(maybeBase)) {
    // Apply postfix operators: [] for arrays and ? for optional types
    while (true) {
      // Array type suffix: []
      if (expectToken(lexer::TokenKind::kLeftBracket)) {
        consumeToken();  // consume '['
        if (!consumeExpectedToken(lexer::TokenKind::kRightBracket)) {
          // Error: expected closing bracket ']'
          return zc::none;
        }
        result = ast::factory::createArrayType(zc::mv(result));
        continue;
      }

      // Optional type suffix: ?
      if (expectToken(lexer::TokenKind::kQuestion)) {
        source::SourceLoc questionLoc = currentToken().getLocation();
        consumeToken();  // consume '?'
        result = ast::factory::createOptionalType(zc::mv(result));
        result = finishNode(zc::mv(result), startLoc, questionLoc);
        continue;
      }

      break;
    }

    return zc::mv(result);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::ArrayType>> Parser::parseArrayType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseArrayType");

  // Parse array type according to the grammar rule:
  // arrayType: postfixType LBRACK RBRACK
  // This handles array types like T[], MyType[], etc.
  // The array suffix [] can be chained: T[][] for multi-dimensional arrays

  const source::SourceLoc startLoc = currentToken().getLocation();

  ZC_IF_SOME(elementType, parsePostfixType()) {
    if (expectToken(lexer::TokenKind::kLeftBracket)) {
      consumeToken();  // consume '['

      if (!consumeExpectedToken(lexer::TokenKind::kRightBracket)) {
        // Error: expected closing bracket ']' for array type
        return zc::none;
      }

      // Create array type node with the element type
      return finishNode(ast::factory::createArrayType(zc::mv(elementType)), startLoc);
    }
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::FunctionType>> Parser::parseFunctionType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseFunctionType");

  // Parse function type according to the grammar rule:
  //
  // functionType: typeParameters? parameterClause (ARROW type raisesClause?)?
  //
  // This handles function types like (a: T, b: U) -> R or (a: T) -> R raises E
  // where raisesClause is optional and specifies error types that the function can raise

  source::SourceLoc startLoc = currentToken().getLocation();

  // Parse optional type parameters: <T, U>
  zc::Vector<zc::Own<ast::TypeParameter>> typeParameters = parseTypeParameters();
  // Parse parameter clause: (param1: Type1, param2: Type2)
  zc::Vector<zc::Own<ast::BindingElement>> parameters = parseParameters();
  // Parse return type: -> type raises error
  ZC_IF_SOME(returnType, parseReturnType()) {
    return finishNode(ast::factory::createFunctionType(zc::mv(typeParameters), zc::mv(parameters),
                                                       zc::mv(returnType)),
                      startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::ParenthesizedType>> Parser::parseParenthesizedType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseParenthesizedType");

  if (!consumeExpectedToken(lexer::TokenKind::kLeftParen)) { return zc::none; }

  ZC_IF_SOME(type, parseType()) {
    if (!consumeExpectedToken(lexer::TokenKind::kRightParen)) { return zc::none; }
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

  if (!consumeExpectedToken(lexer::TokenKind::kLeftBrace)) { return zc::none; }

  zc::Vector<zc::Own<ast::Node>> members;

  // Parse object type members
  if (!expectToken(lexer::TokenKind::kRightBrace)) {
    do {
      // Parse property signature: identifier COLON type
      ZC_IF_SOME(propertyName, parseIdentifier()) {
        if (consumeExpectedToken(lexer::TokenKind::kColon)) {
          ZC_IF_SOME(propertyType, parseType()) {
            // TODO: Implement PropertySignature properly
            // For now, skip adding property signatures to avoid compilation error
            (void)propertyName;  // Suppress unused variable warning
            (void)propertyType;  // Suppress unused variable warning
          }
        }
      }
    } while (consumeExpectedToken(lexer::TokenKind::kComma) ||
             consumeExpectedToken(lexer::TokenKind::kSemicolon));
  }

  if (!consumeExpectedToken(lexer::TokenKind::kRightBrace)) { return zc::none; }

  return ast::factory::createObjectType(zc::mv(members));
}

zc::Maybe<zc::Own<ast::TupleType>> Parser::parseTupleType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseTupleType");

  // Parse tuple type according to the grammar rule:
  //
  // tupleType: LPAREN tupleElementTypes? RPAREN
  //
  // This handles tuple types like (T, U, V) for function parameters and return types
  // Each element can be a named tuple element: (name: T, other: U)

  source::SourceLoc startLoc = currentToken().getLocation();

  if (!consumeExpectedToken(lexer::TokenKind::kLeftParen)) { return zc::none; }

  zc::Vector<zc::Own<ast::Type>> elementTypes;

  if (!expectToken(lexer::TokenKind::kRightParen)) {
    do {
      // Check if this is a named tuple element: name: Type
      if (lookAhead(1).is(lexer::TokenKind::kColon)) {
        // Parse named tuple element
        ZC_IF_SOME(elementName, parseIdentifier()) {
          (void)elementName;  // Suppress unused variable warning
          consumeToken();     // consume ':'
          ZC_IF_SOME(elementType, parseType()) {
            // TODO: Create named tuple element type
            elementTypes.add(zc::mv(elementType));
          }
        }
      } else {
        // Parse regular tuple element
        ZC_IF_SOME(elementType, parseType()) { elementTypes.add(zc::mv(elementType)); }
      }
    } while (consumeExpectedToken(lexer::TokenKind::kComma));
  }

  if (!consumeExpectedToken(lexer::TokenKind::kRightParen)) { return zc::none; }

  return finishNode(ast::factory::createTupleType(zc::mv(elementTypes)), startLoc);
}

zc::Maybe<zc::Own<ast::TypeReference>> Parser::parseTypeReference() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseTypeReference");

  // Parse type reference according to the grammar rule:
  // typeReference: typeName typeArguments?
  // This handles type references like MyType or generic types like MyType<T, U>
  // where typeArguments are optional type parameters in angle brackets

  source::SourceLoc startLoc = currentToken().getLocation();

  ZC_IF_SOME(typeName, parseIdentifier()) {
    // Handle optional type arguments: <T, U, V>
    zc::Maybe<zc::Vector<zc::Own<ast::Type>>> maybeTypeArguments = zc::none;
    if (expectToken(lexer::TokenKind::kLessThan)) {
      zc::Vector<zc::Own<ast::Type>> typeArguments;

      consumeToken();  // consume '<'
      if (!expectToken(lexer::TokenKind::kGreaterThan)) {
        // Parse type argument list
        do {
          ZC_IF_SOME(typeArg, parseType()) { typeArguments.add(zc::mv(typeArg)); }
        } while (consumeExpectedToken(lexer::TokenKind::kComma));
      }

      if (!consumeExpectedToken(lexer::TokenKind::kGreaterThan)) {
        // Error: expected closing angle bracket '>'
        return zc::none;
      }

      maybeTypeArguments = zc::mv(typeArguments);
    }

    return finishNode(
        ast::factory::createTypeReference(zc::mv(typeName), zc::mv(maybeTypeArguments)), startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::PredefinedType>> Parser::parsePredefinedType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parsePredefinedType");

  // Parse predefined type according to the grammar rule:
  // predefinedType: I8 | I32 | I64 | U8 | U16 | U32 | U64 | F32 | F64 | STR | BOOL | NIL | UNIT
  // These are the built-in primitive types in the ZOM language

  const lexer::Token& token = currentToken();
  const source::SourceLoc startLoc = token.getLocation();

  switch (token.getKind()) {
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
    case lexer::TokenKind::kBoolKeyword:
    case lexer::TokenKind::kNullKeyword:
    case lexer::TokenKind::kUnitKeyword: {
      zc::String typeName = token.getText(impl->sourceMgr);
      consumeToken();
      return finishNode(ast::factory::createPredefinedType(zc::mv(typeName)), startLoc);
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
      zc::String opText = currentToken().getText(impl->sourceMgr);
      consumeToken();

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
      consumeToken();

      bool isOptional = false;
      bool isNonNull = false;

      if (expectToken(lexer::TokenKind::kQuestion)) {
        isOptional = true;
        consumeToken();
      } else if (expectToken(lexer::TokenKind::kExclamation)) {
        isNonNull = true;
        consumeToken();
      }

      ZC_IF_SOME(type, parseType()) {
        if (isOptional) {
          return ast::factory::createConditionalAsExpression(zc::mv(expr), zc::mv(type));
        } else if (isNonNull) {
          return ast::factory::createForcedAsExpression(zc::mv(expr), zc::mv(type));
        } else {
          return ast::factory::createAsExpression(zc::mv(expr), zc::mv(type));
        }
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

  consumeToken();

  ZC_IF_SOME(expr, parseUnaryExpression()) {
    return ast::factory::createAwaitExpression(zc::mv(expr));
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::DebuggerStatement>> Parser::parseDebuggerStatement() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseDebuggerStatement");

  source::SourceLoc startLoc = currentToken().getLocation();
  if (!consumeExpectedToken(lexer::TokenKind::kDebuggerKeyword)) { return zc::none; }

  if (!consumeExpectedToken(lexer::TokenKind::kSemicolon)) { return zc::none; }

  return finishNode(ast::factory::createDebuggerStatement(), startLoc);
}

zc::Maybe<zc::Own<ast::NewExpression>> Parser::parseNewExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseNewExpression");

  // newExpression: memberExpression | NEW newExpression;
  // memberExpression: (primaryExpression | superProperty | NEW memberExpression arguments)
  //                   (LBRACK expression RBRACK | PERIOD identifier)*;

  if (!expectToken(lexer::TokenKind::kNewKeyword)) { return zc::none; }

  consumeToken();

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

  const source::SourceLoc startLoc = currentToken().getLocation();

  if (!consumeExpectedToken(lexer::TokenKind::kLeftParen)) { return zc::none; }

  ZC_IF_SOME(expr, parseExpression()) {
    if (!consumeExpectedToken(lexer::TokenKind::kRightParen)) { return zc::none; }

    // Create a parenthesized expression with the parsed expression
    return finishNode(ast::factory::createParenthesizedExpression(zc::mv(expr)), startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::MemberExpression>> Parser::parseMemberExpressionRest(
    zc::Own<ast::MemberExpression> expr, bool allowOptionalChain) {
  while (true) {
    bool questionDotToken = false;
    bool isPropertyAccess = false;

    // Check for optional chaining first
    if (allowOptionalChain && consumeExpectedToken(lexer::TokenKind::kQuestionDot)) {
      questionDotToken = true;
      // After ?., check if next token is identifier (property access) or '[' (element access)
      isPropertyAccess = expectToken(lexer::TokenKind::kIdentifier);
    } else {
      // Check for regular property access
      isPropertyAccess = expectToken(lexer::TokenKind::kPeriod);
      if (isPropertyAccess) {
        consumeToken();  // consume '.'
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
    if (consumeExpectedToken(lexer::TokenKind::kLeftBracket)) {
      ZC_IF_SOME(index, parseExpression()) {
        if (!consumeExpectedToken(lexer::TokenKind::kRightBracket)) { return zc::none; }
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

  consumeToken();  // consume 'super'

  // Create the super identifier as the base expression
  zc::Own<ast::Identifier> expression = ast::factory::createIdentifier(zc::str("super"));

  // Check for type arguments (e.g., super<T>)
  if (expectToken(lexer::TokenKind::kLessThan)) {
    source::SourceLoc errorLoc = currentToken().getLocation();
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
  source::SourceLoc errorLoc = currentToken().getLocation();
  impl->diagnosticEngine.diagnose<diagnostics::DiagID::InvalidChar>(
      errorLoc, zc::str("super must be followed by an argument list or member access"));

  // Try to recover by expecting a dot and parsing the right side
  if (expectToken(lexer::TokenKind::kPeriod)) {
    consumeToken();  // consume '.'
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

source::SourceLoc Parser::getFullStartLoc() const { return impl->lexer.getFullStartLoc(); }

// ================================================================================
// Literal parsing implementations

zc::Maybe<zc::Own<ast::StringLiteral>> Parser::parseStringLiteral() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseStringLiteral");

  const lexer::Token& token = currentToken();
  if (!token.is(lexer::TokenKind::kStringLiteral)) { return zc::none; }

  source::SourceLoc startLoc = token.getLocation();
  zc::String value = token.getText(impl->sourceMgr);
  consumeToken();

  return finishNode(ast::factory::createStringLiteral(zc::mv(value)), startLoc);
}

zc::Maybe<zc::Own<ast::IntegerLiteral>> Parser::parseIntegerLiteral() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseIntegerLiteral");

  const lexer::Token& token = currentToken();
  if (!token.is(lexer::TokenKind::kIntegerLiteral)) { return zc::none; }

  source::SourceLoc startLoc = token.getLocation();
  zc::String value = token.getText(impl->sourceMgr);
  consumeToken();

  int64_t numValue = value.parseAs<int64_t>();
  return finishNode(ast::factory::createIntegerLiteral(numValue), startLoc);
}

zc::Maybe<zc::Own<ast::FloatLiteral>> Parser::parseFloatLiteral() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseFloatLiteral");

  const lexer::Token& token = currentToken();
  if (!token.is(lexer::TokenKind::kFloatLiteral)) { return zc::none; }

  source::SourceLoc startLoc = token.getLocation();
  zc::String value = token.getText(impl->sourceMgr);
  consumeToken();

  double numValue = zc::StringPtr(value).parseAs<double>();
  return finishNode(ast::factory::createFloatLiteral(numValue), startLoc);
}

zc::Maybe<zc::Own<ast::BooleanLiteral>> Parser::parseBooleanLiteral() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseBooleanLiteral");

  const lexer::Token& token = currentToken();
  if (!token.is(lexer::TokenKind::kTrueKeyword) && !token.is(lexer::TokenKind::kFalseKeyword)) {
    return zc::none;
  }

  source::SourceLoc startLoc = token.getLocation();
  bool value = token.is(lexer::TokenKind::kTrueKeyword);
  consumeToken();

  return finishNode(ast::factory::createBooleanLiteral(value), startLoc);
}

zc::Maybe<zc::Own<ast::NullLiteral>> Parser::parseNullLiteral() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseNullLiteral");

  const lexer::Token& token = currentToken();
  if (!token.is(lexer::TokenKind::kNullKeyword)) { return zc::none; }

  source::SourceLoc startLoc = token.getLocation();
  consumeToken();

  return finishNode(ast::factory::createNullLiteral(), startLoc);
}

zc::Maybe<zc::Own<ast::FunctionExpression>> Parser::parseFunctionExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseFunctionExpression");

  // functionExpression:
  //   FUN callSignature LBRACE functionBody RBRACE;
  //
  // callSignature:
  //   typeParameters? parameterClause (ARROW type raisesClause?)?;

  const lexer::Token& token = currentToken();
  if (!token.is(lexer::TokenKind::kFunKeyword)) { return zc::none; }

  source::SourceLoc startLoc = token.getLocation();
  consumeToken();  // consume 'fun'

  // Parse callSignature
  zc::Vector<zc::Own<ast::TypeParameter>> typeParameters = parseTypeParameters();
  // Parse parameter list
  zc::Vector<zc::Own<ast::BindingElement>> parameters = parseParameters();
  // Parse optional return type or error return clause
  zc::Maybe<zc::Own<ast::Type>> returnType = parseReturnType();

  // Parse function body: LBRACE functionBody RBRACE
  if (!expectToken(lexer::TokenKind::kLeftBrace)) { return zc::none; }

  ZC_IF_SOME(body, parseBlockStatement()) {
    return finishNode(
        ast::factory::createFunctionExpression(zc::mv(typeParameters), zc::mv(parameters),
                                               zc::mv(returnType), zc::mv(body)),
        startLoc);
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::OptionalExpression>> Parser::parseOptionalExpression() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseOptionalExpression");

  // optionalExpression:
  //   (memberExpression | callExpression) optionalChain (optionalChain)*;
  // optionalChain:
  //   OPTIONAL_CHAINING identifier (
  //     arguments
  //     | LBRACK expression RBRACK
  //     | PERIOD identifier
  //   )*;

  // First parse the base expression (memberExpression or callExpression)
  zc::Maybe<zc::Own<ast::LeftHandSideExpression>> baseExpr = zc::none;

  // Try to parse as member expression first
  ZC_IF_SOME(memberExpr, parseMemberExpressionOrHigher()) {
    // Check if it can be extended to a call expression
    ZC_IF_SOME(callExpr, parseCallExpressionRest(zc::mv(memberExpr))) {
      baseExpr = zc::mv(callExpr);
    }
    else { baseExpr = zc::mv(memberExpr); }
  }
  else { return zc::none; }

  // Check for optional chaining operator
  if (!expectToken(lexer::TokenKind::kQuestionDot)) { return zc::none; }

  source::SourceLoc startLoc = currentToken().getLocation();
  consumeToken();  // consume '?.'

  // Parse the property access after ?.
  ZC_IF_SOME(property, parseIdentifier()) {
    ZC_IF_SOME(expr, baseExpr) {
      return finishNode(ast::factory::createOptionalExpression(zc::mv(expr), zc::mv(property)),
                        startLoc);
    }
  }

  return zc::none;
}

zc::Maybe<zc::Own<ast::TypeParameter>> Parser::parseTypeParameter() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseTypeParameter");

  // typeParameter: identifier constraint?;
  // constraint: EXTENDS type;

  source::SourceLoc startLoc = currentToken().getLocation();

  ZC_IF_SOME(name, parseIdentifier()) {
    // Optional constraint
    zc::Maybe<zc::Own<ast::Type>> constraint = zc::none;
    if (expectToken(lexer::TokenKind::kExtendsKeyword)) {
      consumeToken();
      constraint = parseType();
    }

    return finishNode(
        ast::factory::createTypeParameterDeclaration(zc::mv(name), zc::mv(constraint)), startLoc);
  }

  return zc::none;
}

zc::Vector<zc::Own<ast::TypeParameter>> Parser::parseTypeParameters() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseTypeParameters");

  zc::Vector<zc::Own<ast::TypeParameter>> typeParameters;

  if (consumeExpectedToken(lexer::TokenKind::kLessThan)) {
    do {
      ZC_IF_SOME(typeParameter, parseTypeParameter()) { typeParameters.add(zc::mv(typeParameter)); }
      else { return zc::Vector<zc::Own<ast::TypeParameter>>(); }
    } while (consumeExpectedToken(lexer::TokenKind::kComma));

    if (!consumeExpectedToken(lexer::TokenKind::kGreaterThan)) {
      return zc::Vector<zc::Own<ast::TypeParameter>>();
    }
  }

  return typeParameters;
}

zc::Vector<zc::Own<ast::BindingElement>> Parser::parseParameters() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseParameters");

  zc::Vector<zc::Own<ast::BindingElement>> parameters;
  if (consumeExpectedToken(lexer::TokenKind::kLeftParen)) {
    // Parse parameterList: parameter (COMMA parameter)*
    do {
      // parameter: bindingIdentifier typeAnnotation? initializer?
      ZC_IF_SOME(param, parseBindingElement()) { parameters.add(zc::mv(param)); }
      else { return zc::Vector<zc::Own<ast::BindingElement>>(); }
    } while (consumeExpectedToken(lexer::TokenKind::kComma));

    if (!consumeExpectedToken(lexer::TokenKind::kRightParen)) {
      return zc::Vector<zc::Own<ast::BindingElement>>();
    }
  }

  return parameters;
}

zc::Maybe<zc::Own<ast::ReturnType>> Parser::parseReturnType() {
  trace::ScopeTracer scopeTracer(trace::TraceCategory::kParser, "parseReturnType");

  // Parse optional return type or error return clause
  //
  // callSignature:
  //   typeParameters? parameterClause (ARROW type raisesClause?)?;

  if (!consumeExpectedToken(lexer::TokenKind::kArrow)) { return zc::none; }

  ZC_IF_SOME(type, parseType()) {
    zc::Maybe<zc::Own<ast::Type>> errorType = zc::none;
    if (consumeExpectedToken(lexer::TokenKind::kRaisesKeyword)) { errorType = parseType(); }
    return ast::factory::createReturnType(zc::mv(type), zc::mv(errorType));
  }

  return zc::none;
}

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
