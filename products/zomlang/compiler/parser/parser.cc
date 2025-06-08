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
#include "zomlang/compiler/ast/statement.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/lexer/lexer.h"
#include "zomlang/compiler/lexer/token.h"
#include "zomlang/compiler/source/location.h"
#include "zomlang/compiler/source/manager.h"

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
      return !currentToken.is(lexer::TokenKind::kEOF);
    default:
      return false;
  }
}

bool Parser::abortParsingListOrMoveToNextToken(ParsingContext context) {
  // Simple error recovery: skip the current token and try again
  impl->consumeToken();
  return false;  // Continue parsing
}

zc::Maybe<zc::Own<ast::Node>> Parser::parse() {
  impl->initializeToken();  // Initialize the first token
  ZC_IF_SOME(sourceFileNode, parseSourceFile()) { return zc::mv(sourceFileNode); }
  return zc::none;
}

zc::Maybe<zc::Own<ast::SourceFile>> Parser::parseSourceFile() {
  // sourceFile: module;
  // module: moduleBody?;
  // moduleBody: moduleItemList;
  // moduleItemList: moduleItem+;

  source::SourceLoc startLoc = impl->peekToken().getLocation();

  zc::Vector<zc::Own<ast::Statement>> statements = parseList<ast::Statement>(
      ParsingContext::kSourceElements, ZC_BIND_METHOD(*this, parseModuleItem));

  source::SourceLoc endLoc = impl->peekToken().getLocation();

  // Create the source file node
  const zc::StringPtr fileName = impl->sourceMgr.getIdentifierForBuffer(impl->bufferId);
  zc::Own<ast::SourceFile> sourceFile =
      ast::factory::createSourceFile(fileName, zc::mv(statements));
  sourceFile->setSourceRange(source::SourceRange(startLoc, endLoc));

  return sourceFile;
}

zc::Maybe<zc::Own<ast::Statement>> Parser::parseModuleItem() {
  // moduleItem:
  //   statementListItem
  //   | exportDeclaration
  //   | importDeclaration;

  const lexer::Token& currentToken = impl->peekToken();

  // Check for import declaration
  if (currentToken.is(lexer::TokenKind::kImportKeyword)) {
    ZC_IF_SOME(importDecl, parseImportDeclaration()) { return zc::mv(importDecl); }
  }

  // Check for export declaration
  if (currentToken.is(lexer::TokenKind::kExportKeyword)) {
    ZC_IF_SOME(exportDecl, parseExportDeclaration()) { return zc::mv(exportDecl); }
  }

  // Otherwise, parse as statement (statementListItem)
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
    zc::Maybe<zc::StringPtr> alias = zc::none;

    // Check for optional AS clause
    const lexer::Token& nextToken = impl->peekToken();
    if (nextToken.is(lexer::TokenKind::kAsKeyword)) {
      impl->consumeToken();  // consume AS

      // Parse identifier name (alias)
      const lexer::Token& aliasToken = impl->peekToken();
      if (aliasToken.is(lexer::TokenKind::kIdentifier)) {
        alias = aliasToken.getText();
        impl->consumeToken();  // consume identifier
      } else {
        // Error: expected identifier after AS
        return zc::none;
      }
    }

    source::SourceLoc endLoc = impl->peekToken().getLocation();

    // Create ImportDeclaration with modulePath and optional alias
    auto importDecl = ast::factory::createImportDeclaration(zc::mv(modulePath), alias);
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
  zc::Vector<zc::StringPtr> identifiers;
  identifiers.add(currentToken.getText());
  impl->consumeToken();  // consume first identifier

  // Parse optional additional identifiers separated by PERIOD
  while (true) {
    const lexer::Token& nextToken = impl->peekToken();
    if (nextToken.is(lexer::TokenKind::kPeriod)) {
      impl->consumeToken();  // consume PERIOD

      const lexer::Token& idToken = impl->peekToken();
      if (idToken.is(lexer::TokenKind::kIdentifier)) {
        identifiers.add(idToken.getText());
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
    zc::StringPtr identifier = nextToken.getText();
    impl->consumeToken();  // consume identifier

    // Check if this is exportRename (identifier AS identifier FROM modulePath)
    const lexer::Token& followingToken = impl->peekToken();
    if (followingToken.is(lexer::TokenKind::kAsKeyword)) {
      impl->consumeToken();  // consume AS

      const lexer::Token& secondIdToken = impl->peekToken();
      if (secondIdToken.is(lexer::TokenKind::kIdentifier)) {
        zc::StringPtr alias = secondIdToken.getText();
        impl->consumeToken();  // consume second identifier

        const lexer::Token& fromToken = impl->peekToken();
        if (fromToken.is(lexer::TokenKind::kFromKeyword)) {
          impl->consumeToken();  // consume FROM

          // Parse modulePath
          ZC_IF_SOME(modulePath, parseModulePath()) {
            source::SourceLoc endLoc = impl->peekToken().getLocation();

            // Create ExportDeclaration with rename info
            auto exportDecl =
                ast::factory::createExportDeclaration(identifier, alias, zc::mv(modulePath));
            exportDecl->setSourceRange(source::SourceRange(startLoc, endLoc));
            return exportDecl;
          }
        }
      }
    } else {
      // Simple exportModule: just bindingIdentifier
      source::SourceLoc endLoc = impl->peekToken().getLocation();

      auto exportDecl = ast::factory::createExportDeclaration(identifier);
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

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
