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

#include "zc/core/common.h"
#include "zomlang/compiler/ast/kinds.h"

namespace zomlang {
namespace compiler {
namespace ast {

class Node;
class Identifier;
class Declaration;

zc::Maybe<const ast::Identifier&> getNameOfDeclaration(const ast::Declaration& node);

/// \brief Check if a node is a property name literal
/// \param node The AST node to check
/// \return true if the node is a valid property name literal, false otherwise
bool isPropertyNameLiteral(const ast::Node& node);

// bool isDeclaration(const ast::Node& node);

bool isExportable(const ast::Node& node);

// ================================================================================
// UTILITY FUNCTIONS
// ================================================================================

/// \brief Check if a syntax element kind represents a keyword
inline bool isKeyword(SyntaxKind kind) {
  return kind >= SyntaxKind::FirstKeyword && kind <= SyntaxKind::LastKeyword;
}

/// \brief Check if a syntax element kind represents a reserved keyword
inline bool isReservedKeyword(SyntaxKind kind) {
  return kind >= SyntaxKind::FirstReservedWord && kind <= SyntaxKind::LastReservedWord;
}

/// \brief Check if a syntax element kind represents an identifier or keyword
inline bool isIdentifierOrKeyword(SyntaxKind kind) {
  return kind == SyntaxKind::Identifier || isKeyword(kind);
}

/// \brief Check if a syntax element kind represents punctuation
inline bool isPunctuation(SyntaxKind kind) {
  return kind >= SyntaxKind::FirstPunctuation && kind <= SyntaxKind::LastPunctuation;
}

/// \brief Check if a syntax element kind represents a statement
inline bool isStatement(SyntaxKind kind) {
  return kind >= SyntaxKind::FirstStatement && kind <= SyntaxKind::LastStatement;
}

}  // namespace ast
}  // namespace compiler
}  // namespace zomlang
