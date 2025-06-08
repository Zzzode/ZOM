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

#pragma once

#include "zc/core/common.h"
#include "zc/core/function.h"
#include "zc/core/memory.h"
#include "zomlang/compiler/ast/ast.h"
#include "zomlang/compiler/ast/statement.h"

namespace zomlang {
namespace compiler {

namespace basic {
struct LangOptions;
}

namespace diagnostics {
class DiagnosticEngine;
}

namespace source {
class BufferId;
class SourceManager;
class SourceRange;
}  // namespace source

// Forward declarations for AST nodes that will be used by the parser.
// These should be defined in ast.h
namespace ast {
class SourceFile;
class ImplementationModule;
class ImplementationModuleElement;
class ImportDeclaration;
class ExportDeclaration;
class ModulePath;
class ImplementationElement;
}  // namespace ast

namespace parser {

enum ParsingContext {
  kSourceElements = 0,  // Parsing source elements (statements, declarations, etc.)
  kBlockElements,       // Parsing block elements (statements, declarations, etc.)
  kClassMembers,        // Parsing class members (fields, methods, etc.)
  kEnumMembers,         // Parsing enum members (values, etc.)
  kCount,
};

/// \brief The parser class.
class Parser {
public:
  Parser(const source::SourceManager& sourceMgr, diagnostics::DiagnosticEngine& diagnosticEngine,
         const basic::LangOptions& langOpts, const source::BufferId& bufferId) noexcept;
  ~Parser() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(Parser);

  /// \brief Parse the source file and return the AST.
  /// \return The AST if parsing succeeded, zc::Nothing otherwise.
  zc::Maybe<zc::Own<ast::Node>> parse();

private:
  void finishNode(ast::Node* node, const source::SourceRange& range);

  template <typename Node>
  zc::Vector<zc::Own<Node>> parseList(ParsingContext context,
                                      zc::Function<zc::Maybe<zc::Own<Node>>()> parseElement) {
    zc::Vector<zc::Own<Node>> result;

    while (!isListTerminator(context)) {
      if (isListElement(context, /*inErrorRecovery*/ false)) {
        ZC_IF_SOME(element, parseElement()) { result.add(zc::mv(element)); }
        continue;
      }

      if (abortParsingListOrMoveToNextToken(context)) { break; }
    }

    return result;
  }

  bool isListTerminator(ParsingContext context) const;
  bool isListElement(ParsingContext context, bool inErrorRecovery) const;

  bool abortParsingListOrMoveToNextToken(ParsingContext context);

private:
  zc::Maybe<zc::Own<ast::SourceFile>> parseSourceFile();
  zc::Maybe<zc::Own<ast::ImportDeclaration>> parseImportDeclaration();
  zc::Maybe<zc::Own<ast::ExportDeclaration>> parseExportDeclaration();
  zc::Maybe<zc::Own<ast::ModulePath>> parseModulePath();
  zc::Maybe<zc::Own<ast::Statement>> parseStatement();
  zc::Maybe<zc::Own<ast::Statement>> parseModuleItem();
  // TODO: Add parseExportImplementationElement if it's a distinct AST node

  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
