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
#include "zomlang/compiler/ast/factory.h"
#include "zomlang/compiler/ast/module.h"
#include "zomlang/compiler/ast/statement.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
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

  const source::BufferId& bufferId;
  const source::SourceManager& sourceMgr;
  diagnostics::DiagnosticEngine& diagnosticEngine;
  lexer::Lexer lexer;  // Made non-const to allow lexing
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
  ZOM_TRACE_EVENT(trace::TraceCategory::kParser, "Error recovery", "Skipping token");

  // Simple error recovery: skip the current token and try again
  impl->consumeToken();
  return false;  // Continue parsing
}

zc::Maybe<zc::Own<ast::Node>> Parser::parse() {
  ZOM_TRACE_FUNCTION(trace::TraceCategory::kParser);

  impl->consumeToken();
  ZC_IF_SOME(sourceFileNode, parseSourceFile()) {
    ZOM_TRACE_EVENT(trace::TraceCategory::kParser, "Parse completed successfully");
    return zc::mv(sourceFileNode);
  }

  ZOM_TRACE_EVENT(trace::TraceCategory::kParser, "Parse failed");
  return zc::none;
}

zc::Maybe<zc::Own<ast::SourceFile>> Parser::parseSourceFile() {
  ZOM_TRACE_SCOPE(trace::TraceCategory::kParser, "parseSourceFile");

  // sourceFile: module;
  // module: moduleBody?;
  // moduleBody: moduleItemList;
  // moduleItemList: moduleItem+;

  source::SourceLoc startLoc = impl->peekToken().getLocation();

  zc::Vector<zc::Own<ast::Statement>> statements = parseList<ast::Statement>(
      ParsingContext::kSourceElements, ZC_BIND_METHOD(*this, parseModuleItem));

  ZOM_TRACE_COUNTER(trace::TraceCategory::kParser, "module_items_parsed", statements.size());

  source::SourceLoc endLoc = impl->peekToken().getLocation();

  // Create the source file node
  zc::StringPtr fileName = impl->sourceMgr.getIdentifierForBuffer(impl->bufferId);
  zc::Own<ast::SourceFile> sourceFile =
      ast::factory::createSourceFile(zc::str(fileName), zc::mv(statements));
  sourceFile->setSourceRange(source::SourceRange(startLoc, endLoc));

  ZOM_TRACE_EVENT(trace::TraceCategory::kParser, "Source file created", fileName);
  return sourceFile;
}

zc::Maybe<zc::Own<ast::Statement>> Parser::parseModuleItem() {
  ZOM_TRACE_SCOPE(trace::TraceCategory::kParser, "parseModuleItem");

  // moduleItem:
  //   statementListItem
  //   | exportDeclaration
  //   | importDeclaration;

  const lexer::Token& currentToken = impl->peekToken();

  // Check for import declaration
  if (currentToken.is(lexer::TokenKind::kImportKeyword)) {
    ZOM_TRACE_EVENT(trace::TraceCategory::kParser, "Parsing import declaration");
    ZC_IF_SOME(importDecl, parseImportDeclaration()) { return zc::mv(importDecl); }
  }

  // Check for export declaration
  if (currentToken.is(lexer::TokenKind::kExportKeyword)) {
    ZOM_TRACE_EVENT(trace::TraceCategory::kParser, "Parsing export declaration");
    ZC_IF_SOME(exportDecl, parseExportDeclaration()) { return zc::mv(exportDecl); }
  }

  // Otherwise, parse as statement (statementListItem)
  ZOM_TRACE_EVENT(trace::TraceCategory::kParser, "Parsing statement");
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
  // statement:
  //   expressionStatement
  //   | declarationStatement

  return zc::none;
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

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
