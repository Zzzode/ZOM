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
#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/ast/expression.h"
#include "zomlang/compiler/ast/module.h"
#include "zomlang/compiler/ast/statement.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/lexer/lexer.h"
#include "zomlang/compiler/lexer/token.h"
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
  diagnostics::DiagnosticEngine& diagnosticEngine;
  lexer::Lexer lexer;  // Made non-const to allow lexing
  lexer::Token currentToken;
};

// ================================================================================
// Parser

Parser::Parser(const source::SourceManager& sourceMgr,
               diagnostics::DiagnosticEngine& diagnosticEngine, const basic::LangOptions& langOpts,
               const source::BufferId& bufferId) noexcept
    : impl(zc::heap<Impl>(sourceMgr, diagnosticEngine, langOpts, bufferId)) {}

Parser::~Parser() noexcept(false) = default;

// Helper function to create a diagnostic for unexpected tokens
// diagnostics::InFlightDiagnostic Parser::Impl::diagnoseUnexpectedToken(const lexer::Token& token,
// lexer::TokenKind expectedKind) {
//   return diagnosticEngine.report(token.getLocation(), diag::err_unexpected_token)
//          << token.getText() << expectedKind; // Assuming TokenKind can be streamed
// }

zc::Maybe<zc::Own<ast::Node>> Parser::parse() {
  impl->initializeToken();  // Initialize the first token
  ZC_IF_SOME(sourceFileNode, parseSourceFile()) { return zc::mv(sourceFileNode); }
  return zc::none;
}

zc::Maybe<zc::Own<ast::SourceFile>> Parser::parseSourceFile() {
  // sourceFile: implementationSourceFile;
  ZC_IF_SOME(implementationModule, parseImplementationModule()) { (void)implementationModule; }
  else {
    // TODO: Add error reporting if parseImplementationModule fails
    return zc::none;
  }
  // TODO: Create and return SourceFile AST node
  // return zc::heap<ast::SourceFile>(zc::move(implementationModule.value()));
  return zc::none;  // Placeholder
}

zc::Maybe<zc::Own<ast::ImplementationModule>> Parser::parseImplementationModule() {
  // implementationModule: implementationModuleElements?;
  // TODO: Implement logic to parse optional implementationModuleElements
  return zc::none;  // Placeholder
}

zc::Maybe<zc::Own<ast::ImplementationModuleElement>> Parser::parseImplementationModuleElement() {
  // implementationModuleElement:
  //  importDeclaration
  //  | exportDeclaration
  //  | implementationElement
  //  | exportImplementationElement;
  // TODO: Implement logic based on the current token
  return zc::none;  // Placeholder
}

zc::Maybe<zc::Own<ast::ImportDeclaration>> Parser::parseImportDeclaration() {
  // importDeclaration: IMPORT modulePath ( AS identifierName )?;
  // TODO: Check for IMPORT token, parse modulePath and optional alias
  return zc::none;  // Placeholder
}

zc::Maybe<zc::Own<ast::ExportDeclaration>> Parser::parseExportDeclaration() {
  // exportDeclaration: EXPORT (exportModule | exportRename);
  // TODO: Check for EXPORT token, then parse exportModule or exportRename
  return zc::none;  // Placeholder
}

zc::Maybe<zc::Own<ast::ModulePath>> Parser::parseModulePath() {
  // modulePath: bindingIdentifier ( PERIOD bindingIdentifier )*;
  // TODO: Parse one or more bindingIdentifiers separated by PERIOD
  return zc::none;  // Placeholder
}

zc::Maybe<zc::Own<ast::ImplementationElement>> Parser::parseImplementationElement() {
  // implementationElement:
  //   lexicalDeclaration
  //   | functionDeclaration
  //   | classDeclaration
  //   | interfaceDeclaration
  //   | typeAliasDeclaration
  //   | enumDeclaration;
  // TODO: Implement logic based on the current token to parse different kinds of declarations
  return zc::none;  // Placeholder
}

// TODO: Add definition for parseExportImplementationElement if it's a distinct AST node
// zc::Maybe<zc::Own<ast::ExportImplementationElement>> Parser::parseExportImplementationElement() {
//   // exportImplementationElement: EXPORT implementationElement;
//   // TODO: Check for EXPORT token, then parse implementationElement
//   return zc::none; // Placeholder
// }

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
