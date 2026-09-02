// Copyright (c) 2026 Zode.Z. All rights reserved
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
// See the License for the specific language governing permissions and limitations under
// the License.

#include "compiler/ide/snapshot-token.h"

namespace zomlang::compiler::ide {

SnapshotTokenCategory projectTokenCategory(ast::SyntaxKind kind) noexcept {
  // Literals are enumerated explicitly: they precede the keyword range and are
  // not covered by any First.../Last... marker.
  switch (kind) {
    case ast::SyntaxKind::Identifier:
      return SnapshotTokenCategory::Identifier;
    case ast::SyntaxKind::StringLiteral:
      return SnapshotTokenCategory::StringLiteral;
    case ast::SyntaxKind::IntegerLiteral:
    case ast::SyntaxKind::BigIntLiteralToken:
    case ast::SyntaxKind::FloatLiteral:
      return SnapshotTokenCategory::NumberLiteral;
    case ast::SyntaxKind::CharacterLiteral:
      return SnapshotTokenCategory::CharacterLiteral;
    case ast::SyntaxKind::NoSubstitutionTemplateLiteral:
    case ast::SyntaxKind::TemplateHead:
    case ast::SyntaxKind::TemplateMiddle:
    case ast::SyntaxKind::TemplateTail:
      return SnapshotTokenCategory::TemplateLiteral;
    case ast::SyntaxKind::Comment:
    case ast::SyntaxKind::SingleLineComment:
    case ast::SyntaxKind::MultiLineComment:
      return SnapshotTokenCategory::Comment;
    default:
      break;
  }
  // The remaining recognized tokens fall inside contiguous kind ranges. The
  // operator span is contiguous from FirstBinaryOperator (`<`) through the
  // error-handling operators (`?!`, `!!`); it covers the binary, bitwise,
  // logical, conditional, assignment, and error-handling groups. LastBinaryOperator
  // marks only the binary-precedence subset, so the span end is the last
  // error-handling operator. The special-operator tokens (At `@`, Hash `#`,
  // Underscore `_`) sit between the operator and punctuation ranges and stay Other.
  if (kind >= ast::SyntaxKind::FirstKeyword && kind <= ast::SyntaxKind::LastKeyword) {
    return SnapshotTokenCategory::Keyword;
  }
  if (kind >= ast::SyntaxKind::FirstBinaryOperator && kind <= ast::SyntaxKind::ErrorUnwrap) {
    return SnapshotTokenCategory::Operator;
  }
  if (kind >= ast::SyntaxKind::FirstPunctuation && kind <= ast::SyntaxKind::LastPunctuation) {
    return SnapshotTokenCategory::Punctuation;
  }
  return SnapshotTokenCategory::Other;
}

}  // namespace zomlang::compiler::ide
