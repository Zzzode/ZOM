// Copyright (c) 2024-2025 Zode.Z. All rights reserved
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
// See the License for the specific language governing permissions and
// limitations under the License.

// Shared parser helper function implementations.
// These were previously defined inline in parser-impl.h's anonymous namespace.
// Per RFC 0002, implementations live in .cc files; the header contains only
// declarations in the parser_helpers namespace.

#include "zomlang/compiler/parser/parser-impl.h"

namespace zomlang {
namespace compiler {
namespace parser {
namespace parser_helpers {

// --- Identifier-like classification ---

bool isIdentifierLike(ast::SyntaxKind kind) {
  return kind == ast::SyntaxKind::Identifier || kind == ast::SyntaxKind::ThisKeyword ||
         kind == ast::SyntaxKind::SuperKeyword;
}

bool isExpressionIdentifierLike(ast::SyntaxKind kind) {
  return isIdentifierLike(kind);
}

bool isPropertyNameLike(ast::SyntaxKind kind) {
  return kind == ast::SyntaxKind::Identifier || lexer::isKeyword(kind);
}

// --- Primitive type keywords ---

bool isPrimitiveTypeKeyword(ast::SyntaxKind kind) {
  switch (kind) {
    case ast::SyntaxKind::I8Keyword:
    case ast::SyntaxKind::I16Keyword:
    case ast::SyntaxKind::I32Keyword:
    case ast::SyntaxKind::I64Keyword:
    case ast::SyntaxKind::U8Keyword:
    case ast::SyntaxKind::U16Keyword:
    case ast::SyntaxKind::U32Keyword:
    case ast::SyntaxKind::U64Keyword:
    case ast::SyntaxKind::F32Keyword:
    case ast::SyntaxKind::F64Keyword:
    case ast::SyntaxKind::BoolKeyword:
    case ast::SyntaxKind::StrKeyword:
    case ast::SyntaxKind::CharKeyword:
    case ast::SyntaxKind::NeverKeyword:
    case ast::SyntaxKind::AnyKeyword:
    case ast::SyntaxKind::NullKeyword:
    case ast::SyntaxKind::UnitKeyword:
      return true;
    default:
      return false;
  }
}

uint8_t predefinedTypeCode(ast::SyntaxKind kind) {
  switch (kind) {
    case ast::SyntaxKind::I8Keyword:
      return 0;
    case ast::SyntaxKind::I16Keyword:
      return 1;
    case ast::SyntaxKind::I32Keyword:
      return 2;
    case ast::SyntaxKind::I64Keyword:
      return 3;
    case ast::SyntaxKind::U8Keyword:
      return 4;
    case ast::SyntaxKind::U16Keyword:
      return 5;
    case ast::SyntaxKind::U32Keyword:
      return 6;
    case ast::SyntaxKind::U64Keyword:
      return 7;
    case ast::SyntaxKind::F32Keyword:
      return 8;
    case ast::SyntaxKind::F64Keyword:
      return 9;
    case ast::SyntaxKind::BoolKeyword:
      return 10;
    case ast::SyntaxKind::StrKeyword:
      return 11;
    case ast::SyntaxKind::CharKeyword:
      return 12;
    case ast::SyntaxKind::NullKeyword:
      return 13;
    case ast::SyntaxKind::UnitKeyword:
      return 14;
    case ast::SyntaxKind::NeverKeyword:
      return 15;
    case ast::SyntaxKind::AnyKeyword:
      return 16;
    default:
      return 16;
  }
}

// --- Numeric literal helpers ---

uint8_t integerBase(zc::StringPtr text) {
  if (text.size() >= 2 && text[0] == '0') {
    const char specifier = text[1];
    if (specifier == 'x' || specifier == 'X') { return 16; }
    if (specifier == 'b' || specifier == 'B') { return 2; }
    if (specifier == 'o' || specifier == 'O') { return 8; }
  }
  return 10;
}

// --- Binary operator precedence (RFC 0002 Section 5, 17-level table) ---

int32_t binaryPrecedence(ast::SyntaxKind kind) {
  switch (kind) {
    // Level 0: Comma (sequence expression) — lowest precedence
    case ast::SyntaxKind::Comma:
      return 0;
    case ast::SyntaxKind::BarBar:
      return 1;
    case ast::SyntaxKind::AmpersandAmpersand:
      return 2;
    case ast::SyntaxKind::Bar:
      return 3;
    case ast::SyntaxKind::Caret:
      return 4;
    case ast::SyntaxKind::Ampersand:
      return 5;
    case ast::SyntaxKind::EqualsEquals:
    case ast::SyntaxKind::ExclamationEquals:
    case ast::SyntaxKind::EqualsEqualsEquals:
    case ast::SyntaxKind::ExclamationEqualsEquals:
      return 6;
    case ast::SyntaxKind::LessThan:
    case ast::SyntaxKind::LessThanEquals:
    case ast::SyntaxKind::GreaterThan:
    case ast::SyntaxKind::GreaterThanEquals:
    case ast::SyntaxKind::IsKeyword:
      return 7;
    case ast::SyntaxKind::LessThanLessThan:
    case ast::SyntaxKind::GreaterThanGreaterThan:
    case ast::SyntaxKind::GreaterThanGreaterThanGreaterThan:
      return 8;
    case ast::SyntaxKind::Plus:
    case ast::SyntaxKind::Minus:
      return 9;
    case ast::SyntaxKind::Asterisk:
    case ast::SyntaxKind::Slash:
    case ast::SyntaxKind::Percent:
      return 10;
    case ast::SyntaxKind::AsteriskAsterisk:
      return 11;
    // Level 12: Nullish coalescing ?? — handled by parseNullCoalesceExpressionAt,
    // not by parseBinaryExpressionAt (creates NullCoalesceExpr, not BinaryExpr).
    // Do NOT add QuestionQuestion here; returning -1 ensures parseBinaryExpressionAt
    // stops at ?? so the dedicated handler can build the correct node type.
    // Level 13: Range operator .. (requires DotDot token; see RFC 0002 Section 5)
    // TODO: Add DotDot SyntaxKind token support. Placeholder for future range operator.
    // Level 14: Type test / cast as
    case ast::SyntaxKind::AsKeyword:
      return 14;
    default:
      return -1;
  }
}

uint16_t binaryOpCode(ast::SyntaxKind kind) {
  switch (kind) {
    case ast::SyntaxKind::Plus:
      return 0;
    case ast::SyntaxKind::Minus:
      return 1;
    case ast::SyntaxKind::Asterisk:
      return 2;
    case ast::SyntaxKind::Slash:
      return 3;
    case ast::SyntaxKind::Percent:
      return 4;
    case ast::SyntaxKind::AsteriskAsterisk:
      return 5;
    case ast::SyntaxKind::LessThanLessThan:
      return 6;
    case ast::SyntaxKind::GreaterThanGreaterThan:
      return 7;
    case ast::SyntaxKind::GreaterThanGreaterThanGreaterThan:
      return 8;
    case ast::SyntaxKind::Ampersand:
      return 9;
    case ast::SyntaxKind::Bar:
      return 10;
    case ast::SyntaxKind::Caret:
      return 11;
    case ast::SyntaxKind::AmpersandAmpersand:
      return 12;
    case ast::SyntaxKind::BarBar:
      return 13;
    case ast::SyntaxKind::EqualsEquals:
      return 14;
    case ast::SyntaxKind::ExclamationEquals:
      return 15;
    case ast::SyntaxKind::EqualsEqualsEquals:
      return 16;
    case ast::SyntaxKind::ExclamationEqualsEquals:
      return 17;
    case ast::SyntaxKind::LessThan:
      return 18;
    case ast::SyntaxKind::LessThanEquals:
      return 19;
    case ast::SyntaxKind::GreaterThan:
      return 20;
    case ast::SyntaxKind::GreaterThanEquals:
      return 21;
    // Extended opcodes for operators handled as dedicated AST node types
    // (NullCoalesceExpr, CastExpr, TypeTestExpr, CommaExpr) but included
    // here for precedence table completeness per RFC 0002 Section 5.
    case ast::SyntaxKind::QuestionQuestion:
      return 22;  // NullCoalesce
    case ast::SyntaxKind::AsKeyword:
      return 23;  // TypeCast
    case ast::SyntaxKind::IsKeyword:
      return 24;  // TypeTest
    case ast::SyntaxKind::Comma:
      return 25;  // Sequence
    default:
      return 0;
  }
}

// --- Unary operators ---

bool isPrefixUnaryOperator(ast::SyntaxKind kind) {
  switch (kind) {
    case ast::SyntaxKind::Plus:
    case ast::SyntaxKind::Minus:
    case ast::SyntaxKind::Exclamation:
    case ast::SyntaxKind::Tilde:
    case ast::SyntaxKind::Asterisk:
    case ast::SyntaxKind::Ampersand:
    case ast::SyntaxKind::PlusPlus:
    case ast::SyntaxKind::MinusMinus:
      return true;
    default:
      return false;
  }
}

uint8_t unaryOpCode(ast::SyntaxKind kind) {
  switch (kind) {
    case ast::SyntaxKind::Plus:
      return static_cast<uint8_t>(ast::UnaryOperatorKind::Plus);
    case ast::SyntaxKind::Minus:
      return static_cast<uint8_t>(ast::UnaryOperatorKind::Minus);
    case ast::SyntaxKind::Exclamation:
      return static_cast<uint8_t>(ast::UnaryOperatorKind::LogicalNot);
    case ast::SyntaxKind::Tilde:
      return static_cast<uint8_t>(ast::UnaryOperatorKind::BitNot);
    case ast::SyntaxKind::Asterisk:
      return static_cast<uint8_t>(ast::UnaryOperatorKind::Deref);
    case ast::SyntaxKind::Ampersand:
      return static_cast<uint8_t>(ast::UnaryOperatorKind::Ref);
    case ast::SyntaxKind::PlusPlus:
      return static_cast<uint8_t>(ast::UnaryOperatorKind::PreIncrement);
    case ast::SyntaxKind::MinusMinus:
      return static_cast<uint8_t>(ast::UnaryOperatorKind::PreDecrement);
    default:
      return static_cast<uint8_t>(ast::UnaryOperatorKind::Plus);
  }
}

// --- Postfix operators ---

bool isPostfixOperator(ast::SyntaxKind kind) {
  switch (kind) {
    case ast::SyntaxKind::PlusPlus:
    case ast::SyntaxKind::MinusMinus:
    case ast::SyntaxKind::ErrorPropagate:
    case ast::SyntaxKind::ErrorUnwrap:
      return true;
    default:
      return false;
  }
}

uint8_t postfixOpCode(ast::SyntaxKind kind) {
  switch (kind) {
    case ast::SyntaxKind::PlusPlus:
      return static_cast<uint8_t>(ast::PostfixOperatorKind::Increment);
    case ast::SyntaxKind::MinusMinus:
      return static_cast<uint8_t>(ast::PostfixOperatorKind::Decrement);
    case ast::SyntaxKind::ErrorPropagate:
      return static_cast<uint8_t>(ast::PostfixOperatorKind::ErrorPropagate);
    case ast::SyntaxKind::ErrorUnwrap:
      return static_cast<uint8_t>(ast::PostfixOperatorKind::ErrorUnwrap);
    default:
      return static_cast<uint8_t>(ast::PostfixOperatorKind::Increment);
  }
}

// --- Macro group helpers ---

bool isMacroGroupOpen(ast::SyntaxKind kind) {
  return kind == ast::SyntaxKind::LeftParen || kind == ast::SyntaxKind::LeftBracket ||
         kind == ast::SyntaxKind::LeftBrace;
}

ast::SyntaxKind macroGroupClose(ast::SyntaxKind kind) {
  switch (kind) {
    case ast::SyntaxKind::LeftParen:
      return ast::SyntaxKind::RightParen;
    case ast::SyntaxKind::LeftBracket:
      return ast::SyntaxKind::RightBracket;
    case ast::SyntaxKind::LeftBrace:
      return ast::SyntaxKind::RightBrace;
    default:
      return ast::SyntaxKind::Unknown;
  }
}

zc::StringPtr macroGroupCloseLabel(ast::SyntaxKind kind) {
  switch (kind) {
    case ast::SyntaxKind::LeftParen:
      return ")"_zc;
    case ast::SyntaxKind::LeftBracket:
      return "]"_zc;
    case ast::SyntaxKind::LeftBrace:
      return "}"_zc;
    default:
      return "delimiter"_zc;
  }
}

ast::MacroBrace macroBraceCode(ast::SyntaxKind kind) {
  switch (kind) {
    case ast::SyntaxKind::LeftParen:
      return ast::MacroBrace::Paren;
    case ast::SyntaxKind::LeftBracket:
      return ast::MacroBrace::Brack;
    case ast::SyntaxKind::LeftBrace:
      return ast::MacroBrace::Brace;
    default:
      return ast::MacroBrace::Paren;
  }
}

// --- Binding declaration helpers ---

ast::BindingDeclarationKind bindingDeclarationKindCode(ast::SyntaxKind kind) {
  switch (kind) {
    case ast::SyntaxKind::LetKeyword:
      return ast::BindingDeclarationKind::Let;
    case ast::SyntaxKind::MutKeyword:
      return ast::BindingDeclarationKind::Mut;
    case ast::SyntaxKind::ConstKeyword:
      return ast::BindingDeclarationKind::Const;
    default:
      ZC_UNREACHABLE;
  }
}

// --- Assignment operators ---

bool isAssignmentOperator(ast::SyntaxKind kind) {
  switch (kind) {
    case ast::SyntaxKind::Equals:
    case ast::SyntaxKind::PlusEquals:
    case ast::SyntaxKind::MinusEquals:
    case ast::SyntaxKind::AsteriskEquals:
    case ast::SyntaxKind::SlashEquals:
    case ast::SyntaxKind::PercentEquals:
    case ast::SyntaxKind::AsteriskAsteriskEquals:
    case ast::SyntaxKind::LessThanLessThanEquals:
    case ast::SyntaxKind::GreaterThanGreaterThanEquals:
    case ast::SyntaxKind::GreaterThanGreaterThanGreaterThanEquals:
    case ast::SyntaxKind::AmpersandEquals:
    case ast::SyntaxKind::BarEquals:
    case ast::SyntaxKind::CaretEquals:
    case ast::SyntaxKind::AmpersandAmpersandEquals:
    case ast::SyntaxKind::BarBarEquals:
    case ast::SyntaxKind::QuestionQuestionEquals:
      return true;
    default:
      return false;
  }
}

uint8_t assignmentOpCode(ast::SyntaxKind kind) {
  switch (kind) {
    case ast::SyntaxKind::Equals:
      return static_cast<uint8_t>(ast::AssignmentOperatorKind::Assign);
    case ast::SyntaxKind::PlusEquals:
      return static_cast<uint8_t>(ast::AssignmentOperatorKind::AddAssign);
    case ast::SyntaxKind::MinusEquals:
      return static_cast<uint8_t>(ast::AssignmentOperatorKind::SubAssign);
    case ast::SyntaxKind::AsteriskEquals:
      return static_cast<uint8_t>(ast::AssignmentOperatorKind::MulAssign);
    case ast::SyntaxKind::SlashEquals:
      return static_cast<uint8_t>(ast::AssignmentOperatorKind::DivAssign);
    case ast::SyntaxKind::PercentEquals:
      return static_cast<uint8_t>(ast::AssignmentOperatorKind::ModAssign);
    case ast::SyntaxKind::AsteriskAsteriskEquals:
      return static_cast<uint8_t>(ast::AssignmentOperatorKind::PowAssign);
    case ast::SyntaxKind::LessThanLessThanEquals:
      return static_cast<uint8_t>(ast::AssignmentOperatorKind::ShlAssign);
    case ast::SyntaxKind::GreaterThanGreaterThanEquals:
      return static_cast<uint8_t>(ast::AssignmentOperatorKind::ShrAssign);
    case ast::SyntaxKind::GreaterThanGreaterThanGreaterThanEquals:
      return static_cast<uint8_t>(ast::AssignmentOperatorKind::UShrAssign);
    case ast::SyntaxKind::AmpersandEquals:
      return static_cast<uint8_t>(ast::AssignmentOperatorKind::BitAndAssign);
    case ast::SyntaxKind::BarEquals:
      return static_cast<uint8_t>(ast::AssignmentOperatorKind::BitOrAssign);
    case ast::SyntaxKind::CaretEquals:
      return static_cast<uint8_t>(ast::AssignmentOperatorKind::BitXorAssign);
    case ast::SyntaxKind::AmpersandAmpersandEquals:
      return static_cast<uint8_t>(ast::AssignmentOperatorKind::LogicalAndAssign);
    case ast::SyntaxKind::BarBarEquals:
      return static_cast<uint8_t>(ast::AssignmentOperatorKind::LogicalOrAssign);
    case ast::SyntaxKind::QuestionQuestionEquals:
      return static_cast<uint8_t>(ast::AssignmentOperatorKind::NullCoalesceAssign);
    default:
      return static_cast<uint8_t>(ast::AssignmentOperatorKind::Assign);
  }
}

// --- Cast mode ---

uint8_t castModeCode(ast::SyntaxKind kind) {
  if (kind == ast::SyntaxKind::Question) { return 1; }
  if (kind == ast::SyntaxKind::Exclamation) { return 2; }
  return 0;
}

// --- Expression boundary helpers ---

bool canEndExpressionBeforeBinary(ast::SyntaxKind kind) {
  switch (kind) {
    case ast::SyntaxKind::Identifier:
    case ast::SyntaxKind::ThisKeyword:
    case ast::SyntaxKind::SuperKeyword:
    case ast::SyntaxKind::TrueKeyword:
    case ast::SyntaxKind::FalseKeyword:
    case ast::SyntaxKind::NullKeyword:
    case ast::SyntaxKind::UnitKeyword:
    case ast::SyntaxKind::IntegerLiteral:
    case ast::SyntaxKind::BigIntLiteralToken:
    case ast::SyntaxKind::FloatLiteral:
    case ast::SyntaxKind::StringLiteral:
    case ast::SyntaxKind::CharacterLiteral:
    case ast::SyntaxKind::NoSubstitutionTemplateLiteral:
    case ast::SyntaxKind::TemplateTail:
    case ast::SyntaxKind::RightParen:
    case ast::SyntaxKind::RightBracket:
    case ast::SyntaxKind::RightBrace:
    case ast::SyntaxKind::PlusPlus:
    case ast::SyntaxKind::MinusMinus:
    case ast::SyntaxKind::ErrorPropagate:
    case ast::SyntaxKind::ErrorUnwrap:
      return true;
    default:
      return false;
  }
}

bool canStartStatementAfterBindingDeclaration(ast::SyntaxKind kind) {
  switch (kind) {
    case ast::SyntaxKind::ModuleKeyword:
    case ast::SyntaxKind::ImportKeyword:
    case ast::SyntaxKind::ExportKeyword:
    case ast::SyntaxKind::MutKeyword:
    case ast::SyntaxKind::LetKeyword:
    case ast::SyntaxKind::ConstKeyword:
    case ast::SyntaxKind::FunKeyword:
    case ast::SyntaxKind::ClassKeyword:
    case ast::SyntaxKind::StructKeyword:
    case ast::SyntaxKind::InterfaceKeyword:
    case ast::SyntaxKind::EnumKeyword:
    case ast::SyntaxKind::ErrorKeyword:
    case ast::SyntaxKind::TypeKeyword:
    case ast::SyntaxKind::AliasKeyword:
    case ast::SyntaxKind::IfKeyword:
    case ast::SyntaxKind::MatchKeyword:
    case ast::SyntaxKind::WhileKeyword:
    case ast::SyntaxKind::DoKeyword:
    case ast::SyntaxKind::ForKeyword:
    case ast::SyntaxKind::BreakKeyword:
    case ast::SyntaxKind::ContinueKeyword:
    case ast::SyntaxKind::ReturnKeyword:
    case ast::SyntaxKind::DebuggerKeyword:
    case ast::SyntaxKind::SpawnKeyword:
    case ast::SyntaxKind::SuspendKeyword:
    case ast::SyntaxKind::Semicolon:
      return true;
    default:
      return false;
  }
}

// --- Unsupported statement keywords ---

bool isUnsupportedStatementKeyword(ast::SyntaxKind kind) {
  switch (kind) {
    case ast::SyntaxKind::AwaitKeyword:
    case ast::SyntaxKind::ThrowKeyword:
    case ast::SyntaxKind::TryKeyword:
    case ast::SyntaxKind::CatchKeyword:
    case ast::SyntaxKind::FinallyKeyword:
    case ast::SyntaxKind::NamespaceKeyword:
    case ast::SyntaxKind::DeclareKeyword:
    case ast::SyntaxKind::VarKeyword:
    case ast::SyntaxKind::ActorKeyword:
    case ast::SyntaxKind::ChannelKeyword:
    case ast::SyntaxKind::GeneratorKeyword:
      return true;
    default:
      return false;
  }
}

// --- Template literal helpers ---

bool isTemplateLiteralToken(ast::SyntaxKind kind) {
  return kind == ast::SyntaxKind::NoSubstitutionTemplateLiteral ||
         kind == ast::SyntaxKind::TemplateHead;
}

bool canPrecedeTaggedTemplate(ast::SyntaxKind kind) {
  return kind == ast::SyntaxKind::Identifier || kind == ast::SyntaxKind::GreaterThan ||
         kind == ast::SyntaxKind::RightParen || kind == ast::SyntaxKind::RightBracket ||
         kind == ast::SyntaxKind::ThisKeyword || kind == ast::SyntaxKind::SuperKeyword;
}

// --- Declaration modifier helpers ---

bool isInterfaceModifier(ast::SyntaxKind kind) {
  switch (kind) {
    case ast::SyntaxKind::AbstractKeyword:
    case ast::SyntaxKind::OverrideKeyword:
    case ast::SyntaxKind::PrivateKeyword:
    case ast::SyntaxKind::ProtectedKeyword:
    case ast::SyntaxKind::PublicKeyword:
    case ast::SyntaxKind::ReadonlyKeyword:
    case ast::SyntaxKind::StaticKeyword:
      return true;
    default:
      return false;
  }
}

bool isDeclarationModifier(ast::SyntaxKind kind) {
  switch (kind) {
    case ast::SyntaxKind::AbstractKeyword:
    case ast::SyntaxKind::MutKeyword:
    case ast::SyntaxKind::MutatingKeyword:
    case ast::SyntaxKind::OverrideKeyword:
    case ast::SyntaxKind::PrivateKeyword:
    case ast::SyntaxKind::ProtectedKeyword:
    case ast::SyntaxKind::PublicKeyword:
    case ast::SyntaxKind::ReadonlyKeyword:
    case ast::SyntaxKind::StaticKeyword:
      return true;
    default:
      return false;
  }
}

bool isDeclarationHead(ast::SyntaxKind kind) {
  switch (kind) {
    case ast::SyntaxKind::LetKeyword:
    case ast::SyntaxKind::MutKeyword:
    case ast::SyntaxKind::ConstKeyword:
    case ast::SyntaxKind::FunKeyword:
    case ast::SyntaxKind::ClassKeyword:
    case ast::SyntaxKind::StructKeyword:
    case ast::SyntaxKind::InterfaceKeyword:
    case ast::SyntaxKind::EnumKeyword:
    case ast::SyntaxKind::ErrorKeyword:
    case ast::SyntaxKind::TypeKeyword:
    case ast::SyntaxKind::AliasKeyword:
      return true;
    default:
      return false;
  }
}

bool isNamedTypeDeclarationHead(ast::SyntaxKind kind) {
  switch (kind) {
    case ast::SyntaxKind::ClassKeyword:
    case ast::SyntaxKind::StructKeyword:
    case ast::SyntaxKind::InterfaceKeyword:
    case ast::SyntaxKind::EnumKeyword:
    case ast::SyntaxKind::ErrorKeyword:
      return true;
    default:
      return false;
  }
}

// --- Object literal helpers ---

bool isInvalidObjectLiteralPropertyName(ast::SyntaxKind kind) {
  switch (kind) {
    case ast::SyntaxKind::StringLiteral:
    case ast::SyntaxKind::IntegerLiteral:
    case ast::SyntaxKind::FloatLiteral:
    case ast::SyntaxKind::CharacterLiteral:
      return true;
    default:
      return false;
  }
}

// --- Pattern / expression token classification ---

bool isLiteralPatternToken(ast::SyntaxKind kind) {
  switch (kind) {
    case ast::SyntaxKind::TrueKeyword:
    case ast::SyntaxKind::FalseKeyword:
    case ast::SyntaxKind::NullKeyword:
    case ast::SyntaxKind::IntegerLiteral:
    case ast::SyntaxKind::BigIntLiteralToken:
    case ast::SyntaxKind::FloatLiteral:
    case ast::SyntaxKind::StringLiteral:
    case ast::SyntaxKind::NoSubstitutionTemplateLiteral:
    case ast::SyntaxKind::CharacterLiteral:
      return true;
    default:
      return false;
  }
}

bool isLiteralExpressionToken(ast::SyntaxKind kind) {
  switch (kind) {
    case ast::SyntaxKind::TrueKeyword:
    case ast::SyntaxKind::FalseKeyword:
    case ast::SyntaxKind::NullKeyword:
    case ast::SyntaxKind::UnitKeyword:
    case ast::SyntaxKind::IntegerLiteral:
    case ast::SyntaxKind::BigIntLiteralToken:
    case ast::SyntaxKind::FloatLiteral:
    case ast::SyntaxKind::StringLiteral:
    case ast::SyntaxKind::NoSubstitutionTemplateLiteral:
    case ast::SyntaxKind::CharacterLiteral:
      return true;
    default:
      return false;
  }
}

// --- Attribute helpers ---

bool isAttributePathSegment(ast::SyntaxKind kind) {
  return kind == ast::SyntaxKind::Identifier || lexer::isKeyword(kind);
}

bool isAttributeStart(ast::SyntaxKind first, ast::SyntaxKind second) {
  return first == ast::SyntaxKind::Hash && second == ast::SyntaxKind::LeftBracket;
}

bool isTopLevelCfgAttributeTarget(ast::SyntaxKind kind) {
  switch (kind) {
    case ast::SyntaxKind::ModuleDeclaration:
    case ast::SyntaxKind::ImportDeclaration:
    case ast::SyntaxKind::ExportDeclaration:
    case ast::SyntaxKind::LetStmt:
    case ast::SyntaxKind::FunctionDecl:
    case ast::SyntaxKind::ClassDecl:
    case ast::SyntaxKind::StructDecl:
    case ast::SyntaxKind::InterfaceDecl:
    case ast::SyntaxKind::EnumDeclaration:
    case ast::SyntaxKind::ErrorDecl:
    case ast::SyntaxKind::AliasDecl:
    case ast::SyntaxKind::ExternBlock:
    case ast::SyntaxKind::BlockStmt:
      return true;
    default:
      return false;
  }
}

// --- Token labeling ---

zc::StringPtr tokenLabel(const lexer::Token& token) {
  ZC_IF_SOME(text, lexer::Token::getStaticTextForTokenKind(token.getKind())) { return text; }
  if (token.getValue().size() != 0) { return token.getValue(); }
  return "<token>"_zc;
}

}  // namespace parser_helpers
}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
