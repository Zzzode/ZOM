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

// Internal implementation header for the parser. Do NOT include from
// public headers or from outside the parser library.

#pragma once

#include <cstddef>
#include <cstdint>

#include "zc/core/debug.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/generated/node-factory.h"
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/schema-verifier.h"
#include "zomlang/compiler/basic/string-pool.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/diagnostic-ids.h"
#include "zomlang/compiler/lexer/token.h"
#include "zomlang/compiler/lexer/utils.h"
#include "zomlang/compiler/parser/parser-context.h"
#include "zomlang/compiler/parser/parser.h"
#include "zomlang/compiler/parser/token-cursor.h"
#include "zomlang/compiler/source/manager.h"
#include "zomlang/compiler/trace/trace.h"

namespace zomlang {
namespace compiler {
namespace parser {

// --- Shared helper functions ---

namespace {

[[maybe_unused]] bool isIdentifierLike(ast::SyntaxKind kind) {
  return kind == ast::SyntaxKind::Identifier || kind == ast::SyntaxKind::ThisKeyword ||
         kind == ast::SyntaxKind::SuperKeyword;
}

[[maybe_unused]] bool isExpressionIdentifierLike(ast::SyntaxKind kind) {
  return isIdentifierLike(kind);
}

[[maybe_unused]] bool isPropertyNameLike(ast::SyntaxKind kind) {
  return kind == ast::SyntaxKind::Identifier || lexer::isKeyword(kind);
}

[[maybe_unused]] bool isPrimitiveTypeKeyword(ast::SyntaxKind kind) {
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

[[maybe_unused]] uint8_t predefinedTypeCode(ast::SyntaxKind kind) {
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

[[maybe_unused]] uint8_t integerBase(zc::StringPtr text) {
  if (text.size() >= 2 && text[0] == '0') {
    const char specifier = text[1];
    if (specifier == 'x' || specifier == 'X') { return 16; }
    if (specifier == 'b' || specifier == 'B') { return 2; }
    if (specifier == 'o' || specifier == 'O') { return 8; }
  }
  return 10;
}

[[maybe_unused]] int32_t binaryPrecedence(ast::SyntaxKind kind) {
  switch (kind) {
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
    default:
      return -1;
  }
}

[[maybe_unused]] uint16_t binaryOpCode(ast::SyntaxKind kind) {
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
    default:
      return 0;
  }
}

[[maybe_unused]] bool isPrefixUnaryOperator(ast::SyntaxKind kind) {
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

[[maybe_unused]] uint8_t unaryOpCode(ast::SyntaxKind kind) {
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

[[maybe_unused]] bool isPostfixOperator(ast::SyntaxKind kind) {
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

[[maybe_unused]] uint8_t postfixOpCode(ast::SyntaxKind kind) {
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

[[maybe_unused]] bool isMacroGroupOpen(ast::SyntaxKind kind) {
  return kind == ast::SyntaxKind::LeftParen || kind == ast::SyntaxKind::LeftBracket ||
         kind == ast::SyntaxKind::LeftBrace;
}

[[maybe_unused]] ast::SyntaxKind macroGroupClose(ast::SyntaxKind kind) {
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

[[maybe_unused]] zc::StringPtr macroGroupCloseLabel(ast::SyntaxKind kind) {
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

[[maybe_unused]] ast::MacroBrace macroBraceCode(ast::SyntaxKind kind) {
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

[[maybe_unused]] ast::BindingDeclarationKind bindingDeclarationKindCode(ast::SyntaxKind kind) {
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

[[maybe_unused]] bool isAssignmentOperator(ast::SyntaxKind kind) {
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

[[maybe_unused]] uint8_t assignmentOpCode(ast::SyntaxKind kind) {
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

[[maybe_unused]] uint8_t castModeCode(ast::SyntaxKind kind) {
  if (kind == ast::SyntaxKind::Question) { return 1; }
  if (kind == ast::SyntaxKind::Exclamation) { return 2; }
  return 0;
}

[[maybe_unused]] bool canEndExpressionBeforeBinary(ast::SyntaxKind kind) {
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

[[maybe_unused]] bool canStartStatementAfterBindingDeclaration(ast::SyntaxKind kind) {
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

[[maybe_unused]] bool isUnsupportedStatementKeyword(ast::SyntaxKind kind) {
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

[[maybe_unused]] bool isTemplateLiteralToken(ast::SyntaxKind kind) {
  return kind == ast::SyntaxKind::NoSubstitutionTemplateLiteral ||
         kind == ast::SyntaxKind::TemplateHead;
}

[[maybe_unused]] bool canPrecedeTaggedTemplate(ast::SyntaxKind kind) {
  return kind == ast::SyntaxKind::Identifier || kind == ast::SyntaxKind::GreaterThan ||
         kind == ast::SyntaxKind::RightParen || kind == ast::SyntaxKind::RightBracket ||
         kind == ast::SyntaxKind::ThisKeyword || kind == ast::SyntaxKind::SuperKeyword;
}

[[maybe_unused]] bool isInterfaceModifier(ast::SyntaxKind kind) {
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

[[maybe_unused]] bool isDeclarationModifier(ast::SyntaxKind kind) {
  switch (kind) {
    case ast::SyntaxKind::AbstractKeyword:
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

[[maybe_unused]] bool isDeclarationHead(ast::SyntaxKind kind) {
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

[[maybe_unused]] bool isNamedTypeDeclarationHead(ast::SyntaxKind kind) {
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

[[maybe_unused]] bool isInvalidObjectLiteralPropertyName(ast::SyntaxKind kind) {
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

[[maybe_unused]] bool isLiteralPatternToken(ast::SyntaxKind kind) {
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

[[maybe_unused]] bool isLiteralExpressionToken(ast::SyntaxKind kind) {
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

[[maybe_unused]] bool isAttributePathSegment(ast::SyntaxKind kind) {
  return kind == ast::SyntaxKind::Identifier || lexer::isKeyword(kind);
}

[[maybe_unused]] bool isAttributeStart(ast::SyntaxKind first, ast::SyntaxKind second) {
  return first == ast::SyntaxKind::Hash && second == ast::SyntaxKind::LeftBracket;
}

[[maybe_unused]] bool isTopLevelCfgAttributeTarget(ast::SyntaxKind kind) {
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

[[maybe_unused]] zc::StringPtr tokenLabel(const lexer::Token& token) {
  ZC_IF_SOME(text, lexer::Token::getStaticTextForTokenKind(token.getKind())) { return text; }
  if (token.getValue().size() != 0) { return token.getValue(); }
  return "<token>"_zc;
}

}  // namespace

class AstFactory final : public ast::TypedNodeFactory<AstFactory> {
public:
  ast::NodeList makeList(zc::ArrayPtr<const ast::NodeId> nodes) { return builder.makeList(nodes); }

  ast::IdentList makeIdentList(zc::ArrayPtr<const ast::IdentId> ids) {
    return builder.makeIdentList(ids);
  }

  ast::StringId internString(zc::StringPtr value) { return builder.internString(value); }

  ast::IdentId internIdent(zc::StringPtr value) { return builder.internIdent(value); }

  ast::BigIntId internBigInt(zc::StringPtr value) { return builder.internBigInt(value); }

  ast::FloatId internFloat(zc::StringPtr value) { return builder.internFloat(value); }

  void setRoot(ast::NodeId id) { builder.setRoot(id); }

  ast::Tree finish() { return builder.finish(); }

private:
  template <typename>
  friend class ast::TypedNodeFactory;

  ast::NodeId makeTypedNode(ast::SyntaxKind kind, source::SourceRange range,
                            ast::NodePayload payload = {}) {
    return builder.makeNode(kind, zc::mv(range), payload);
  }

  ast::TreeBuilder builder;
};

// --- Parser::Impl declarations ---

struct Parser::Impl {
  Impl(const source::SourceManager& sourceMgr, diagnostics::DiagnosticEngine& diagnosticEngine,
       const basic::LangOptions& langOpts, basic::StringPool& stringPool,
       const source::BufferId& bufferId);

  const source::SourceManager& sourceMgr;
  diagnostics::DiagnosticEngine& diagnosticEngine;
  source::BufferId bufferId;
  ParserContext context;

  enum class RecoveryContext : uint8_t {
    SourceFile,
    Declaration,
    Statement,
    Expression,
    Type,
    Pattern,
  };

  struct RecoveryFrame {
    RecoveryContext context = RecoveryContext::SourceFile;
    size_t anchor = 0;
    ast::SyntaxKind syncSet[32] = {};
    uint8_t syncCount = 0;
    bool consumed = false;
    size_t suppressedUntil = 0;
  };

  class RecoveryFrameScope {
  public:
    RecoveryFrameScope(const Impl& parser, RecoveryContext context, size_t anchor);
    ~RecoveryFrameScope();

    size_t finish(size_t position) const;

    RecoveryFrameScope(const RecoveryFrameScope&) = delete;
    RecoveryFrameScope& operator=(const RecoveryFrameScope&) = delete;

  private:
    const Impl& parser;
  };

  mutable zc::Vector<RecoveryFrame> recoveryFrames;

  RecoveryFrame makeRecoveryFrame(RecoveryContext context, size_t anchor) const;

  void pushRecoveryFrame(RecoveryContext context, size_t anchor) const;

  void popRecoveryFrame() const;

  void markRecoveryConsumed(size_t position) const;

  const lexer::Token& tokenAt(size_t index) const;

  ast::SyntaxKind kindAt(size_t index) const;

  bool isAtEnd(size_t index) const;

  TokenCursor tokenCursorAt(size_t index) const;

  source::SourceRange rangeFor(size_t start, size_t end) const;

  ast::IdentId internIdent(AstFactory& builder, size_t index) const;

  ast::StringId internString(AstFactory& builder, size_t index) const;

  bool tokenTextEquals(size_t index, zc::StringPtr expected) const;

  bool isSoftKeyword(size_t index, zc::StringPtr expected) const;

  bool isExternDeclarationStart(size_t index, size_t limit) const;

  bool isSoftDeclarationHead(size_t index, size_t limit) const;

  bool parseExternAbi(size_t index, ast::Abi& abi) const;

  bool rangeIsWrapped(size_t start, size_t end, ast::SyntaxKind open, ast::SyntaxKind close) const;

  size_t findMatchingRightBrace(size_t openIndex, size_t limit) const;

  size_t findMatchingRightBracket(size_t openIndex, size_t limit) const;

  size_t findMatchingMacroGroup(size_t openIndex, size_t limit) const;

  bool isMacroInvocationStart(size_t start, size_t limit) const;

  size_t findMacroInvocationEnd(size_t start, size_t limit) const;

  bool isOuterAttributeStart(size_t index, size_t limit) const;

  size_t skipOuterAttributePrefix(size_t start, size_t end) const;

  bool isIdentifierText(size_t index, zc::StringPtr text) const;

  bool isStandaloneDynTypeRange(size_t start, size_t end) const;

  size_t consumeBalancedUntil(TokenCursor& cursor, size_t limit, ast::SyntaxKind needle) const;

  size_t consumeBalancedTypeUntil(TokenCursor& cursor, size_t limit, ast::SyntaxKind needle) const;

  size_t consumeBalancedIdentifierUntil(TokenCursor& cursor, size_t limit,
                                        zc::StringPtr text) const;

  size_t consumeBalancedTypeIdentifierUntil(TokenCursor& cursor, size_t limit,
                                            zc::StringPtr text) const;

  void addNodeIfPresent(zc::Vector<ast::NodeId>& nodes, ast::NodeId node) const;

  ast::IdentList makeIdentList(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId makeModulePath(AstFactory& builder, size_t start, size_t end) const;

  bool isModulePathSeparatorAt(size_t index, size_t end) const;

  size_t modulePathSeparatorWidth(size_t index, size_t end) const;

  bool modulePathSeparatorPrecedesGroup(size_t index, size_t end) const;

  size_t findModulePathEnd(size_t start, size_t end) const;

  size_t findModuleSpecifierGroupOpen(size_t pathEnd, size_t end) const;

  ast::NodeId makeImportSpecifier(AstFactory& builder, size_t nameIndex, size_t aliasIndex,
                                  size_t end) const;

  ast::NodeId makeExportSpecifier(AstFactory& builder, size_t nameIndex, size_t aliasIndex,
                                  size_t end) const;

  void recoverModuleSpecifier(TokenCursor& cursor, size_t end) const;

  ast::NodeId parseImportSpecifier(AstFactory& builder, TokenCursor& cursor, size_t end) const;

  ast::NodeId parseExportSpecifier(AstFactory& builder, TokenCursor& cursor, size_t end) const;

  zc::Vector<ast::NodeId> parseImportSpecifierList(AstFactory& builder, size_t start,
                                                   size_t end) const;

  zc::Vector<ast::NodeId> parseExportSpecifierList(AstFactory& builder, size_t start,
                                                   size_t end) const;

  ast::NodeId makeAttributePath(AstFactory& builder, size_t start, size_t end) const;

  size_t findAttributePathEnd(size_t start, size_t end) const;

  uint32_t attributePathSegmentCount(size_t start, size_t end) const;

  bool isWhitelistedBareAttribute(size_t start, size_t end) const;

  void diagnoseImportPathSyntax(size_t clauseStart, size_t clauseEnd, size_t pathEnd,
                                size_t groupOpen) const;

  bool isZomCfgAttributePath(size_t start, size_t end) const;

  bool containsUnmodeledRangeOperator(size_t start, size_t end) const;

  void diagnoseCfgAttributeArgs(size_t start, size_t end) const;

  ast::NodeId parseAttribute(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseOuterAttributeList(AstFactory& builder, size_t start, size_t end) const;

  bool outerAttributePrefixContainsZomCfg(size_t start, size_t end) const;

  ast::NodeId makeStatementListItem(AstFactory& builder, ast::NodeId item,
                                    source::SourceRange range,
                                    ast::NodeId attrs = ast::NodeId()) const;

  void emitUnexpected(const lexer::Token& where) const;

  source::SourceLoc diagnosticLoc(size_t index) const;

  void diagnoseExpressionExpected(size_t index) const;

  bool modifierGroupContains(size_t start, size_t end, ast::SyntaxKind needle, size_t& found) const;

  void diagnoseDeclarationModifierGroup(size_t start, size_t end) const;

  bool diagnoseUnsupportedVarianceInTypeParameters(size_t openAngle, size_t closeAngle) const;

  bool diagnoseTypeParameterListSyntax(size_t openAngle, size_t closeAngle) const;

  void diagnoseDeclarationTypeParameterSyntax(size_t afterName, size_t limit) const;

  ast::NodeId parseRequiredExpression(AstFactory& builder, size_t start, size_t end) const;

  bool requireTrailingSemicolon(size_t start, size_t end) const;

  bool isInterfaceElementHead(size_t index, int32_t interfaceBodyDepth) const;

  bool isInterfaceMethodInitializer(size_t index, int32_t interfaceBodyDepth) const;

  bool followsFieldTypeColonWithoutSemicolon(size_t index) const;

  size_t consumeMemberBoundary(size_t start, size_t limit) const;

  void diagnoseMissingFieldMemberSemicolon(size_t start, size_t end) const;

  void diagnoseNamedTypeBody(size_t bodyOpen, size_t bodyClose, ast::SyntaxKind kind) const;

  bool looksLikeObjectLiteralExpression(size_t start, size_t end) const;

  bool isStructLiteralTypeReference(size_t start, size_t end) const;

  size_t findTypePathEnd(size_t start, size_t end) const;

  size_t findStructLiteralBrace(size_t start, size_t end) const;

  bool isDefinitelyNonLValueOperand(size_t start, size_t end) const;

  void diagnoseTokenPatterns();

  size_t effectiveStatementStart(size_t start, size_t end) const;

  struct TypeParseResult {
    ast::NodeId node;
    size_t next = 0;
  };

  size_t findMatchingAngleClose(size_t openIndex, size_t limit) const;

  bool consumeBalancedAngleList(TokenCursor& cursor, size_t limit) const;

  size_t functionTypeParameterTypeStart(TokenCursor& cursor, size_t limit) const;

  ast::NodeList parseFunctionTypeParameters(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseTupleTypeRange(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseObjectTypeRange(AstFactory& builder, size_t start, size_t end) const;

  TypeParseResult parseTypeExpression(AstFactory& builder, TokenCursor& cursor, size_t limit) const;

  TypeParseResult parseTypeExpressionAt(AstFactory& builder, size_t start, size_t limit) const;

  TypeParseResult parseUnionType(AstFactory& builder, TokenCursor& cursor, size_t limit) const;

  TypeParseResult parseIntersectionType(AstFactory& builder, TokenCursor& cursor,
                                        size_t limit) const;

  TypeParseResult parsePostfixType(AstFactory& builder, TokenCursor& cursor, size_t limit) const;

  TypeParseResult parseFunctionType(AstFactory& builder, TokenCursor& cursor, size_t limit) const;

  TypeParseResult parseParenthesizedOrTupleType(AstFactory& builder, TokenCursor& cursor,
                                                size_t limit) const;

  TypeParseResult parseBracketedType(AstFactory& builder, TokenCursor& cursor, size_t limit) const;

  TypeParseResult parseTypeQuery(AstFactory& builder, TokenCursor& cursor, size_t limit) const;

  ast::NodeList parseTypeArgumentList(AstFactory& builder, TokenCursor& cursor, size_t limit,
                                      size_t& physicalEnd) const;

  TypeParseResult parseNamedType(AstFactory& builder, TokenCursor& cursor, size_t limit) const;

  TypeParseResult parseAtomType(AstFactory& builder, TokenCursor& cursor, size_t limit) const;

  ast::NodeId parseTypeRange(AstFactory& builder, size_t start, size_t end) const;

  size_t findExpressionBinaryOperator(size_t start, size_t end) const;

  size_t findExpressionAssignmentOperator(size_t start, size_t end) const;

  size_t findExpressionConditionalColon(size_t question, size_t end) const;

  size_t consumeCommaDelimitedItem(TokenCursor& cursor, size_t end) const;

  ast::NodeId parseExpressionList(AstFactory& builder, size_t start, size_t end,
                                  ast::SyntaxKind containerKind) const;

  ast::NodeId parseCommaExpression(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeList parseExpressionArguments(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeList parseTypeArguments(AstFactory& builder, size_t start, size_t end) const;

  size_t findTrailingCallOpen(size_t start, size_t end) const;

  size_t findTrailingIndexOpen(size_t start, size_t end) const;

  size_t findTrailingTypeArgumentOpen(size_t start, size_t end) const;

  size_t findTrailingMemberOperator(size_t start, size_t end) const;

  bool canUseRangeAsCallCallee(size_t start, size_t end) const;

  ast::NodeId parseCallExpression(AstFactory& builder, size_t start, size_t openParen,
                                  size_t end) const;

  ast::NodeId parseNewExpression(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId makeEmptyMacroPattern(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId makeEmptyMacroTokenTree(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseMacroInvocationExpression(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseUnsafeBlockExpression(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseSpawnExpression(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseCastExpression(AstFactory& builder, size_t start, size_t asIndex,
                                  size_t end) const;

  ast::NodeId parseImportCallExpression(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseCaptureItem(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeList parseCaptureList(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseFunctionExpression(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseLambdaExpression(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeList parseObjectLiteralProperties(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseObjectLiteralExpression(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseStructLiteralExpression(AstFactory& builder, size_t start, size_t brace,
                                           size_t end) const;

  ast::NodeId parseTemplateLiteralExpression(AstFactory& builder, size_t start, size_t end) const;

  struct ExpressionParseResult {
    ast::NodeId node;
    size_t next = 0;
  };

  ExpressionParseResult parseExpressionAt(AstFactory& builder, size_t start, size_t limit) const;

  ExpressionParseResult parseCommaExpressionAt(AstFactory& builder, size_t start,
                                               size_t limit) const;

  ExpressionParseResult parseAssignmentExpressionAt(AstFactory& builder, size_t start,
                                                    size_t limit) const;

  ExpressionParseResult parseConditionalExpressionAt(AstFactory& builder, size_t start,
                                                     size_t limit) const;

  ExpressionParseResult parseErrorDefaultExpressionAt(AstFactory& builder, size_t start,
                                                      size_t limit) const;

  ExpressionParseResult parseNullCoalesceExpressionAt(AstFactory& builder, size_t start,
                                                      size_t limit) const;

  ExpressionParseResult parseBinaryExpressionAt(AstFactory& builder, size_t start, size_t limit,
                                                int32_t minPrecedence) const;

  ExpressionParseResult parseUnaryExpressionAt(AstFactory& builder, size_t start,
                                               size_t limit) const;

  ExpressionParseResult parsePostfixExpressionAt(AstFactory& builder, size_t start,
                                                 size_t limit) const;

  ExpressionParseResult parsePrimaryExpressionAt(AstFactory& builder, size_t start,
                                                 size_t limit) const;

  ast::NodeId parseExpressionRange(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parsePatternRange(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId makeEmptyClassMemberList(AstFactory& builder, source::SourceRange range) const;

  ast::NodeId makeEmptyEnumVariantList(AstFactory& builder, source::SourceRange range) const;

  size_t recoverFunctionParameter(TokenCursor& cursor, size_t closeParen) const;

  ast::NodeId parseFunctionParameter(AstFactory& builder, TokenCursor& cursor,
                                     size_t closeParen) const;

  ast::NodeList parseFunctionParameterNodeList(AstFactory& builder, size_t openParen,
                                               size_t closeParen) const;

  ast::NodeId parseFunctionParameterList(AstFactory& builder, size_t openParen,
                                         size_t closeParen) const;

  size_t consumeSimpleStatementEnd(size_t start, size_t limit) const;

  size_t consumeSpawnStatementEnd(size_t start, size_t limit) const;

  size_t consumeExternDeclarationEnd(size_t start, size_t limit) const;

  size_t findBindingDeclarationRecoveryStart(size_t start, size_t limit) const;

  size_t consumeBindingDeclarationEnd(size_t start, size_t limit) const;

  size_t consumeBracedBodyEnd(size_t bodyOpen, size_t limit) const;

  size_t consumeStatementBodyEnd(size_t bodyStart, size_t limit) const;

  size_t consumeConditionBodyStart(size_t start, size_t limit) const;

  struct IfStatementParts {
    size_t condStart = 0;
    size_t condEnd = 0;
    size_t thenStart = 0;
    size_t thenEnd = 0;
    size_t elseIndex = 0;
    size_t elseStart = 0;
    size_t end = 0;
  };

  IfStatementParts parseIfStatementParts(size_t start, size_t limit) const;

  size_t consumeIfStatementEnd(size_t start, size_t limit) const;

  struct WhileStatementParts {
    size_t condStart = 0;
    size_t condEnd = 0;
    size_t bodyStart = 0;
    size_t bodyEnd = 0;
    size_t end = 0;
  };

  WhileStatementParts parseWhileStatementParts(size_t start, size_t limit) const;

  size_t consumeWhileStatementEnd(size_t start, size_t limit) const;

  struct MatchStatementParts {
    size_t scrutineeStart = 0;
    size_t scrutineeEnd = 0;
    size_t bodyOpen = 0;
    size_t bodyClose = 0;
    size_t end = 0;
  };

  MatchStatementParts parseMatchStatementParts(size_t start, size_t limit) const;

  size_t consumeMatchStatementEnd(size_t start, size_t limit) const;

  struct DoWhileStatementParts {
    size_t bodyStart = 0;
    size_t bodyEnd = 0;
    size_t whileIndex = 0;
    size_t condStart = 0;
    size_t condEnd = 0;
    size_t end = 0;
  };

  DoWhileStatementParts parseDoWhileStatementParts(size_t start, size_t limit) const;

  size_t consumeDoWhileStatementEnd(size_t start, size_t limit) const;

  size_t consumeLabeledStatementEnd(size_t start, size_t limit) const;

  size_t consumeTypeLike(size_t start, size_t limit) const;

  size_t findFunctionBodyOpenAfterParams(size_t closeParen, size_t limit) const;

  struct FunctionDeclarationParts {
    size_t nameIndex = 0;
    size_t openParen = 0;
    size_t closeParen = 0;
    size_t headerEnd = 0;
    size_t bodyOpen = 0;
    size_t end = 0;
    size_t arrow = 0;
    size_t raises = 0;
  };

  FunctionDeclarationParts parseFunctionDeclarationParts(size_t start, size_t limit) const;

  size_t consumeFunctionDeclarationEnd(size_t start, size_t limit) const;

  size_t consumeExportDeclarationEnd(size_t start, size_t limit) const;

  size_t consumeNamedTypeDeclarationEnd(size_t start, size_t limit) const;

  size_t consumeBracedDeclarationEnd(size_t start, size_t limit) const;

  struct SourceElementBoundary {
    size_t start = 0;
    size_t nodeStart = 0;
    size_t head = 0;
    size_t end = 0;
    ast::SyntaxKind kind = ast::SyntaxKind::Unknown;
  };

  struct SourceElementParseResult {
    ast::NodeId node;
    ast::NodeId attrs;
    SourceElementBoundary boundary;
  };

  struct ForStatementParts {
    size_t headerStart = 0;
    size_t headerEnd = 0;
    size_t bodyStart = 0;
    size_t bodyEnd = 0;
    size_t end = 0;
    size_t firstSemi = 0;
    size_t secondSemi = 0;
    size_t inIndex = 0;
    ast::SyntaxKind kind = ast::SyntaxKind::ForStmt;
  };

  ForStatementParts parseForStatementParts(size_t head, size_t limit) const;

  SourceElementBoundary consumeSourceElement(TokenCursor& cursor, size_t limit) const;

  SourceElementParseResult parseSourceElement(AstFactory& builder, TokenCursor& cursor,
                                              size_t limit) const;

  ast::NodeId parseBlock(AstFactory& builder, size_t openBrace, size_t limit,
                         bool allowFinalExpression = false) const;

  ast::NodeId parseStatementBody(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseModuleDeclaration(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseImportDeclaration(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseExportDeclaration(AstFactory& builder, size_t start, size_t end) const;

  struct VariableDeclaratorParseResult {
    ast::NodeId node;
    size_t next = 0;
  };

  size_t consumeVariableDeclaratorPattern(TokenCursor& cursor, size_t limit) const;

  bool isInitializerGenericAngle(size_t openAngle, size_t limit) const;

  size_t consumeVariableInitializer(TokenCursor& cursor, size_t limit) const;

  VariableDeclaratorParseResult parseVariableDeclarator(AstFactory& builder, TokenCursor& cursor,
                                                        size_t limit) const;

  ast::NodeId parseVariableDeclaratorList(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseLetStatement(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseReturnStatement(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseSuspendStatement(AstFactory& builder, size_t start, size_t end) const;

  size_t findMatchingRightParen(size_t openParen, size_t limit) const;

  void conditionRangeAfterKeyword(size_t start, size_t end, size_t& condStart, size_t& condEnd,
                                  size_t& bodyStart) const;

  ast::NodeId parseIfStatement(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseWhileStatement(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseDoWhileStatement(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseBreakStatement(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseContinueStatement(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseLabeledStatement(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseForStatement(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseForInStatement(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseMatchStatement(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseExternFunctionDecl(AstFactory& builder, size_t start, size_t end,
                                      ast::Abi abi) const;

  ast::NodeId parseExternVarDecl(AstFactory& builder, size_t start, size_t end, ast::Abi abi) const;

  ast::NodeId parseExternTypeAliasDecl(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseExternBlockDeclaration(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseMacroRulesDeclaration(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseFunctionDeclaration(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseNamedTypeDeclaration(AstFactory& builder, size_t start, size_t end,
                                        ast::SyntaxKind kind) const;

  ast::NodeId parseErrorDeclaration(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseAliasDeclaration(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseImplInterfaceBound(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId makeImplIfaceList(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseStandaloneImplDeclaration(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseExpressionStatement(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseExpressionStatementWithoutSemicolon(AstFactory& builder, size_t start,
                                                       size_t end) const;

  ast::NodeId parseSpawnStatement(AstFactory& builder, size_t start, size_t end) const;

  ast::NodeId parseSourceElementOfKind(AstFactory& builder, size_t start, size_t end,
                                       ast::SyntaxKind kind) const;

  bool canContinueLetInitializerBefore(size_t index) const;

  ast::Tree buildTree();
};

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
