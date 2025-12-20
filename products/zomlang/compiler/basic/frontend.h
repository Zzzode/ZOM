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

#include "zc/core/memory.h"

namespace zomlang {
namespace compiler {

namespace source {
class SourceManager;
class BufferId;
}  // namespace source

namespace diagnostics {
class DiagnosticEngine;
}

namespace ast {
class Node;
}

namespace symbol {
class SymbolTable;
}

namespace basic {

class StringPool;
struct LangOptions;

/// \brief Perform parsing on a single source buffer
/// \param sm Source manager containing the source buffer
/// \param diagnosticEngine Diagnostic engine for error reporting
/// \param langOpts Language options for parsing
/// \param bufferId Buffer ID of the source to parse
/// \return Parsed AST node or none if parsing failed
zc::Maybe<zc::Own<ast::Node>> performParse(const source::SourceManager& sm,
                                           diagnostics::DiagnosticEngine& diagnosticEngine,
                                           const LangOptions& langOpts,
                                           basic::StringPool& stringPool,
                                           const source::BufferId& bufferId);

/// \brief Perform binding on a parsed AST to create symbols
/// \param symbolTable Symbol table for managing symbols and scopes
/// \param diagnosticEngine Diagnostic engine for error reporting
/// \param ast AST node to bind (must be a SourceFile)
/// \return True if binding succeeded, false if errors occurred
bool performBind(symbol::SymbolTable& symbolTable, diagnostics::DiagnosticEngine& diagnosticEngine,
                 ast::Node& ast);

}  // namespace basic
}  // namespace compiler
}  // namespace zomlang
