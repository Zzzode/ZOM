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
#include "zc/core/memory.h"

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
}  // namespace source

// Forward declarations for AST nodes that will be used by the parser.
// These should be defined in ast.h
namespace ast {
class Node;
class SourceFile;
class ImplementationModule;
class ImplementationModuleElement;
class ImportDeclaration;
class ExportDeclaration;
class ModulePath;
class ImplementationElement;
}  // namespace ast

namespace parser {

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
  zc::Maybe<zc::Own<ast::SourceFile>> parseSourceFile();
  zc::Maybe<zc::Own<ast::ImplementationModule>> parseImplementationModule();
  zc::Maybe<zc::Own<ast::ImplementationModuleElement>> parseImplementationModuleElement();
  zc::Maybe<zc::Own<ast::ImportDeclaration>> parseImportDeclaration();
  zc::Maybe<zc::Own<ast::ExportDeclaration>> parseExportDeclaration();
  zc::Maybe<zc::Own<ast::ModulePath>> parseModulePath();
  zc::Maybe<zc::Own<ast::ImplementationElement>> parseImplementationElement();
  // TODO: Add parseExportImplementationElement if it's a distinct AST node

  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
