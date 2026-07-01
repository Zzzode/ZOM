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
#include "zomlang/compiler/ast/generated/node-payload.h"
#include "zomlang/compiler/ast/schema-verifier.h"
#include "zomlang/compiler/basic/string-pool.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/diagnostic-ids.h"
#include "zomlang/compiler/lexer/lexer.h"
#include "zomlang/compiler/lexer/utils.h"
#include "zomlang/compiler/parser/parser-context.h"
#include "zomlang/compiler/source/manager.h"
#include "zomlang/compiler/trace/trace.h"
#include "zomlang/compiler/parser/parser.h"
#include "zomlang/compiler/parser/parser-context.h"
#include "zomlang/compiler/parser/token-cursor.h"

namespace zomlang {
namespace compiler {
namespace parser {

// --- Shared helper functions ---

namespace {


void writeNode(ast::NodePayload& payload, uint32_t word, ast::NodeId id) {
  payload.words[word] = id.value;
}

void writeString(ast::NodePayload& payload, uint32_t word, ast::StringId id) {
  payload.words[word] = id.value;
}

void writeIdent(ast::NodePayload& payload, uint32_t word, ast::IdentId id) {
  payload.words[word] = id.value;
}

void writeBigInt(ast::NodePayload& payload, uint32_t word, ast::BigIntId id) {
  payload.words[word] = id.value;
}

void writeFloat(ast::NodePayload& payload, uint32_t word, ast::FloatId id) {
  payload.words[word] = id.value;
}

void writeNodeList(ast::NodePayload& payload, uint32_t firstWord, uint32_t sizeWord,
                   ast::NodeList list) {
  payload.words[firstWord] = list.first;
  payload.words[sizeWord] = list.size;
}

void writeIdentList(ast::NodePayload& payload, uint32_t firstWord, uint32_t sizeWord,
                    ast::IdentList list) {
  payload.words[firstWord] = list.first;
  payload.words[sizeWord] = list.size;
}

bool isIdentifierLike(ast::SyntaxKind kind) {
  return kind == ast::SyntaxKind::Identifier || kind == ast::SyntaxKind::ThisKeyword ||
         kind == ast::SyntaxKind::SuperKeyword;
}

bool isExpressionIdentifierLike(ast::SyntaxKind kind) {
  return isIdentifierLike(kind) || kind == ast::SyntaxKind::OptionalKeyword;
}

bool isPropertyNameLike(ast::SyntaxKind kind) {
  return kind == ast::SyntaxKind::Identifier || lexer::isKeyword(kind);
}

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

uint8_t integerBase(zc::StringPtr text) {
  if (text.size() >= 2 && text[0] == '0') {
    const char specifier = text[1];
    if (specifier == 'x' || specifier == 'X') { return 16; }
    if (specifier == 'b' || specifier == 'B') { return 2; }
    if (specifier == 'o' || specifier == 'O') { return 8; }
  }
  return 10;
}

int32_t binaryPrecedence(ast::SyntaxKind kind) {
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
    default:
      return 0;
  }
}

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

uint32_t macroBraceCode(ast::SyntaxKind kind) {
  switch (kind) {
    case ast::SyntaxKind::LeftParen:
      return static_cast<uint32_t>(ast::MacroBrace::Paren);
    case ast::SyntaxKind::LeftBracket:
      return static_cast<uint32_t>(ast::MacroBrace::Brack);
    case ast::SyntaxKind::LeftBrace:
      return static_cast<uint32_t>(ast::MacroBrace::Brace);
    default:
      return static_cast<uint32_t>(ast::MacroBrace::Paren);
  }
}

uint8_t bindingDeclarationKindCode(ast::SyntaxKind kind) {
  switch (kind) {
    case ast::SyntaxKind::LetKeyword:
      return static_cast<uint8_t>(ast::BindingDeclarationKind::Let);
    case ast::SyntaxKind::MutKeyword:
      return static_cast<uint8_t>(ast::BindingDeclarationKind::Mut);
    case ast::SyntaxKind::ConstKeyword:
      return static_cast<uint8_t>(ast::BindingDeclarationKind::Const);
    default:
      ZC_UNREACHABLE;
  }
}

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

uint8_t castModeCode(ast::SyntaxKind kind) {
  if (kind == ast::SyntaxKind::Question) { return 1; }
  if (kind == ast::SyntaxKind::Exclamation) { return 2; }
  return 0;
}

bool canEndExpressionBeforeBinary(ast::SyntaxKind kind) {
  switch (kind) {
    case ast::SyntaxKind::Identifier:
    case ast::SyntaxKind::ThisKeyword:
    case ast::SyntaxKind::SuperKeyword:
    case ast::SyntaxKind::OptionalKeyword:
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

bool startsTemplateSubstitution(ast::SyntaxKind kind) {
  return kind == ast::SyntaxKind::TemplateHead || kind == ast::SyntaxKind::TemplateMiddle;
}

bool continuesTemplateSubstitution(ast::SyntaxKind kind) {
  return kind == ast::SyntaxKind::TemplateMiddle || kind == ast::SyntaxKind::TemplateTail;
}

bool isTemplateLiteralToken(ast::SyntaxKind kind) {
  return kind == ast::SyntaxKind::NoSubstitutionTemplateLiteral ||
         kind == ast::SyntaxKind::TemplateHead;
}

bool canPrecedeTaggedTemplate(ast::SyntaxKind kind) {
  return kind == ast::SyntaxKind::Identifier || kind == ast::SyntaxKind::GreaterThan ||
         kind == ast::SyntaxKind::RightParen || kind == ast::SyntaxKind::RightBracket ||
         kind == ast::SyntaxKind::ThisKeyword || kind == ast::SyntaxKind::SuperKeyword;
}

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

bool isAttributePathSegment(ast::SyntaxKind kind) {
  return kind == ast::SyntaxKind::Identifier || lexer::isKeyword(kind);
}

int32_t typeAngleCloseCount(ast::SyntaxKind kind) {
  switch (kind) {
    case ast::SyntaxKind::GreaterThan:
      return 1;
    case ast::SyntaxKind::GreaterThanGreaterThan:
      return 2;
    case ast::SyntaxKind::GreaterThanGreaterThanGreaterThan:
      return 3;
    default:
      return 0;
  }
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

zc::StringPtr tokenLabel(const lexer::Token& token) {
  ZC_IF_SOME(text, lexer::Token::getStaticTextForTokenKind(token.getKind())) { return text; }
  if (token.getValue().size() != 0) { return token.getValue(); }
  return "<token>"_zc;
}

ast::NodePayload makeSourceFilePayload(ast::StringId fileName, ast::NodeId module,
                                       ast::NodeList statements) {
  ast::NodePayload payload;
  payload.words[ast::kSourceFileFileNameWord] = fileName.value;
  payload.words[ast::kSourceFileModuleWord] = module.value;
  payload.words[ast::kSourceFileStatementsFirstWord] = statements.first;
  payload.words[ast::kSourceFileStatementsSizeWord] = statements.size;
  return payload;
}

}  // namespace

// --- Parser::Impl with all inline methods ---

struct Parser::Impl {
  Impl(const source::SourceManager& sourceMgr, diagnostics::DiagnosticEngine& diagnosticEngine,
       const basic::LangOptions& langOpts, basic::StringPool& stringPool,
       const source::BufferId& bufferId, ParseMode mode = ParseMode::Strict)
      : sourceMgr(sourceMgr),
        diagnosticEngine(diagnosticEngine),
        lexer(sourceMgr, diagnosticEngine, langOpts, stringPool, bufferId),
        bufferId(bufferId),
        context(sourceMgr, diagnosticEngine, bufferId),
        parseMode(mode) {}

  const source::SourceManager& sourceMgr;
  diagnostics::DiagnosticEngine& diagnosticEngine;
  lexer::Lexer lexer;
  lexer::Token token;
  source::BufferId bufferId;
  zc::Vector<lexer::Token> tokens;
  ParserContext context;
  ParseMode parseMode;

  void lexAll() {
    tokens.clear();
    bool insideTemplateSubstitution = false;
    int32_t templateBraceDepth = 0;

    do {
      lexer.lex(token);

      if (insideTemplateSubstitution) {
        if (token.is(ast::SyntaxKind::LeftBrace)) {
          ++templateBraceDepth;
        } else if (token.is(ast::SyntaxKind::RightBrace)) {
          if (templateBraceDepth > 0) {
            --templateBraceDepth;
          } else {
            static_cast<void>(lexer.reScanTemplateToken());
            token = lexer.getCurrentState().token;
            insideTemplateSubstitution = continuesTemplateSubstitution(token.getKind()) &&
                                         token.getKind() != ast::SyntaxKind::TemplateTail;
            templateBraceDepth = 0;
          }
        }
      }

      if (startsTemplateSubstitution(token.getKind())) {
        insideTemplateSubstitution = true;
        templateBraceDepth = 0;
      }

      tokens.add(token);
    } while (!token.is(ast::SyntaxKind::EndOfFile));

    if (insideTemplateSubstitution) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(token.getLocation(), "}"_zc);
    }
    context.resetTokens(tokens.asPtr());
  }

  size_t tokenCountWithoutEof() const { return context.tokenCountWithoutEof(); }

  const lexer::Token& tokenAt(size_t index) const { return context.tokenAt(index); }

  ast::SyntaxKind kindAt(size_t index) const { return context.kindAt(index); }

  TokenCursor tokenCursorAt(size_t index) const {
    TokenCursor cursor(tokens.asPtr());
    cursor.moveTo(index);
    return cursor;
  }

  source::SourceRange rangeFor(size_t start, size_t end) const {
    return context.rangeFor(start, end);
  }

  ast::IdentId internIdent(ast::TreeBuilder& builder, size_t index) const {
    if (index >= tokenCountWithoutEof()) { return ast::IdentId(); }
    zc::StringPtr text = tokenAt(index).getValue();
    if (text.size() == 0) { text = tokenLabel(tokenAt(index)); }
    return builder.internIdent(text);
  }

  ast::StringId internString(ast::TreeBuilder& builder, size_t index) const {
    if (index >= tokenCountWithoutEof()) { return ast::StringId(); }
    zc::StringPtr text = tokenAt(index).getValue();
    if (text.size() == 0) { text = tokenLabel(tokenAt(index)); }
    return builder.internString(text);
  }

  bool tokenTextEquals(size_t index, zc::StringPtr expected) const {
    if (index >= tokenCountWithoutEof()) { return false; }
    zc::StringPtr text = tokenAt(index).getValue();
    if (text.size() == 0) { text = tokenLabel(tokenAt(index)); }
    return text == expected;
  }

  bool isSoftKeyword(size_t index, zc::StringPtr expected) const {
    return index < tokenCountWithoutEof() && kindAt(index) == ast::SyntaxKind::Identifier &&
           tokenTextEquals(index, expected);
  }

  bool isExternDeclarationStart(size_t index, size_t limit) const {
    if (index >= limit) { return false; }
    if (isSoftKeyword(index, "extern"_zc)) { return true; }
    return isSoftKeyword(index, "unsafe"_zc) && index + 1 < limit &&
           isSoftKeyword(index + 1, "extern"_zc);
  }

  bool isSoftDeclarationHead(size_t index, size_t limit) const {
    if (index >= limit) { return false; }
    if (isSoftKeyword(index, "macro"_zc) || isExternDeclarationStart(index, limit) ||
        isSoftKeyword(index, "impl"_zc)) {
      return true;
    }
    return isSoftKeyword(index, "unsafe"_zc) && index + 1 < limit &&
           isSoftKeyword(index + 1, "impl"_zc);
  }

  bool parseExternAbi(size_t index, uint32_t& abi) const {
    abi = static_cast<uint32_t>(ast::Abi::Cdecl);
    if (index >= tokenCountWithoutEof() || kindAt(index) != ast::SyntaxKind::StringLiteral) {
      return true;
    }

    const zc::StringPtr text = tokenAt(index).getValue();
    if (text == "C"_zc || text == "Cdecl"_zc) {
      abi = static_cast<uint32_t>(ast::Abi::Cdecl);
      return true;
    }
    if (text == "system"_zc) {
      abi = static_cast<uint32_t>(ast::Abi::Stdcall);
      return true;
    }
    if (text == "zom-cdecl"_zc) {
      abi = static_cast<uint32_t>(ast::Abi::ZomNative);
      return true;
    }

    diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
        tokenAt(index).getLocation());
    return false;
  }

  bool rangeIsWrapped(size_t start, size_t end, ast::SyntaxKind open, ast::SyntaxKind close) const {
    if (end <= start + 1 || kindAt(start) != open || kindAt(end - 1) != close) { return false; }

    int32_t depth = 0;
    for (size_t index = start; index < end; ++index) {
      const ast::SyntaxKind kind = kindAt(index);
      if (kind == open) { ++depth; }
      if (kind == close) {
        --depth;
        if (depth == 0 && index + 1 < end) { return false; }
      }
    }
    return depth == 0;
  }

  size_t findMatchingRightBrace(size_t openIndex, size_t limit) const {
    if (openIndex >= limit || kindAt(openIndex) != ast::SyntaxKind::LeftBrace) { return limit; }

    int32_t depth = 0;
    for (size_t index = openIndex; index < limit; ++index) {
      const ast::SyntaxKind kind = kindAt(index);
      if (kind == ast::SyntaxKind::LeftBrace) { ++depth; }
      if (kind == ast::SyntaxKind::RightBrace) {
        --depth;
        if (depth == 0) { return index; }
      }
    }
    return limit;
  }

  size_t findMatchingRightBracket(size_t openIndex, size_t limit) const {
    if (openIndex >= limit || kindAt(openIndex) != ast::SyntaxKind::LeftBracket) { return limit; }

    int32_t parenDepth = 0;
    int32_t bracketDepth = 0;
    int32_t braceDepth = 0;
    for (size_t index = openIndex; index < limit; ++index) {
      const ast::SyntaxKind kind = kindAt(index);
      if (kind == ast::SyntaxKind::LeftParen) {
        ++parenDepth;
      } else if (kind == ast::SyntaxKind::RightParen) {
        if (parenDepth > 0) { --parenDepth; }
      } else if (kind == ast::SyntaxKind::LeftBrace) {
        ++braceDepth;
      } else if (kind == ast::SyntaxKind::RightBrace) {
        if (braceDepth > 0) { --braceDepth; }
      } else if (kind == ast::SyntaxKind::LeftBracket) {
        ++bracketDepth;
      } else if (kind == ast::SyntaxKind::RightBracket) {
        if (bracketDepth > 0) { --bracketDepth; }
        if (bracketDepth == 0 && parenDepth == 0 && braceDepth == 0) { return index; }
      }
    }
    return limit;
  }

  size_t findMatchingMacroGroup(size_t openIndex, size_t limit) const {
    if (openIndex >= limit || !isMacroGroupOpen(kindAt(openIndex))) { return limit; }

    const ast::SyntaxKind open = kindAt(openIndex);
    const ast::SyntaxKind close = macroGroupClose(open);
    int32_t depth = 0;
    for (size_t index = openIndex; index < limit; ++index) {
      const ast::SyntaxKind kind = kindAt(index);
      if (kind == open) { ++depth; }
      if (kind == close) {
        --depth;
        if (depth == 0) { return index; }
      }
    }
    return limit;
  }

  bool isMacroInvocationStart(size_t start, size_t limit) const {
    return start + 2 < limit && kindAt(start) == ast::SyntaxKind::Identifier &&
           kindAt(start + 1) == ast::SyntaxKind::Exclamation && isMacroGroupOpen(kindAt(start + 2));
  }

  size_t findMacroInvocationEnd(size_t start, size_t limit) const {
    if (!isMacroInvocationStart(start, limit)) { return start; }

    const size_t close = findMatchingMacroGroup(start + 2, limit);
    return close < limit ? close + 1 : limit;
  }

  bool isOuterAttributeStart(size_t index, size_t limit) const {
    return index + 1 < limit && isAttributeStart(kindAt(index), kindAt(index + 1)) &&
           tokenAt(index).getRange().getEnd() == tokenAt(index + 1).getRange().getStart();
  }

  size_t skipOuterAttributePrefix(size_t start, size_t end) const {
    size_t cursor = start;
    while (isOuterAttributeStart(cursor, end)) {
      const size_t closeBracket = findMatchingRightBracket(cursor + 1, end);
      if (closeBracket >= end) { break; }
      cursor = closeBracket + 1;
    }
    return cursor;
  }

  size_t findTopLevelToken(size_t start, size_t end, ast::SyntaxKind needle) const {
    int32_t parenDepth = 0;
    int32_t bracketDepth = 0;
    int32_t braceDepth = 0;
    for (size_t index = start; index < end; ++index) {
      const ast::SyntaxKind kind = kindAt(index);
      if (kind == needle && parenDepth == 0 && bracketDepth == 0 && braceDepth == 0) {
        return index;
      }

      if (kind == ast::SyntaxKind::LeftParen) {
        ++parenDepth;
      } else if (kind == ast::SyntaxKind::RightParen) {
        if (parenDepth > 0) { --parenDepth; }
      } else if (kind == ast::SyntaxKind::LeftBracket) {
        ++bracketDepth;
      } else if (kind == ast::SyntaxKind::RightBracket) {
        if (bracketDepth > 0) { --bracketDepth; }
      } else if (kind == ast::SyntaxKind::LeftBrace) {
        ++braceDepth;
      } else if (kind == ast::SyntaxKind::RightBrace) {
        if (braceDepth > 0) { --braceDepth; }
      }
    }
    return end;
  }

  size_t findTopLevelIdentifierText(size_t start, size_t end, zc::StringPtr text) const {
    int32_t parenDepth = 0;
    int32_t bracketDepth = 0;
    int32_t braceDepth = 0;
    for (size_t index = start; index < end; ++index) {
      const ast::SyntaxKind kind = kindAt(index);
      if (kind == ast::SyntaxKind::Identifier && parenDepth == 0 && bracketDepth == 0 &&
          braceDepth == 0 && tokenAt(index).getValue() == text) {
        return index;
      }

      if (kind == ast::SyntaxKind::LeftParen) {
        ++parenDepth;
      } else if (kind == ast::SyntaxKind::RightParen) {
        if (parenDepth > 0) { --parenDepth; }
      } else if (kind == ast::SyntaxKind::LeftBracket) {
        ++bracketDepth;
      } else if (kind == ast::SyntaxKind::RightBracket) {
        if (bracketDepth > 0) { --bracketDepth; }
      } else if (kind == ast::SyntaxKind::LeftBrace) {
        ++braceDepth;
      } else if (kind == ast::SyntaxKind::RightBrace) {
        if (braceDepth > 0) { --braceDepth; }
      }
    }
    return end;
  }

  bool isIdentifierText(size_t index, zc::StringPtr text) const {
    return index < tokenCountWithoutEof() && kindAt(index) == ast::SyntaxKind::Identifier &&
           tokenAt(index).getValue() == text;
  }

  bool isStandaloneDynTypeRange(size_t start, size_t end) const {
    return start + 1 == end && isIdentifierText(start, "dyn"_zc);
  }

  size_t findTopLevelTypeToken(size_t start, size_t end, ast::SyntaxKind needle) const {
    int32_t parenDepth = 0;
    int32_t bracketDepth = 0;
    int32_t braceDepth = 0;
    int32_t angleDepth = 0;
    for (size_t index = start; index < end; ++index) {
      const ast::SyntaxKind kind = kindAt(index);
      if (kind == needle && parenDepth == 0 && bracketDepth == 0 && braceDepth == 0 &&
          angleDepth == 0) {
        return index;
      }

      if (kind == ast::SyntaxKind::LeftParen) {
        ++parenDepth;
      } else if (kind == ast::SyntaxKind::RightParen) {
        if (parenDepth > 0) { --parenDepth; }
      } else if (kind == ast::SyntaxKind::LeftBracket) {
        ++bracketDepth;
      } else if (kind == ast::SyntaxKind::RightBracket) {
        if (bracketDepth > 0) { --bracketDepth; }
      } else if (kind == ast::SyntaxKind::LeftBrace) {
        ++braceDepth;
      } else if (kind == ast::SyntaxKind::RightBrace) {
        if (braceDepth > 0) { --braceDepth; }
      } else if (kind == ast::SyntaxKind::LessThan) {
        ++angleDepth;
      } else {
        const int32_t closeCount = typeAngleCloseCount(kind);
        if (closeCount > 0 && angleDepth > 0) {
          angleDepth = closeCount >= angleDepth ? 0 : angleDepth - closeCount;
        }
      }
    }
    return end;
  }

  void addNodeIfPresent(zc::Vector<ast::NodeId>& nodes, ast::NodeId node) const {
    if (node) { nodes.add(node); }
  }

  ast::IdentList makeIdentList(ast::TreeBuilder& builder, size_t start, size_t end) const {
    zc::Vector<ast::IdentId> segments;
    for (size_t index = start; index < end; ++index) {
      if (isAttributePathSegment(kindAt(index)) || kindAt(index) == ast::SyntaxKind::ThisKeyword) {
        segments.add(internIdent(builder, index));
      }
    }
    return builder.makeIdentList(segments.asPtr());
  }

  ast::NodeId makeModulePath(ast::TreeBuilder& builder, size_t start, size_t end) const {
    ast::NodePayload payload;
    writeIdentList(payload, ast::kModulePathSegmentsFirstWord, ast::kModulePathSegmentsSizeWord,
                   makeIdentList(builder, start, end));
    return builder.makeNode(ast::SyntaxKind::ModulePath, rangeFor(start, end), payload);
  }

  bool isModulePathSeparatorAt(size_t index, size_t end) const {
    if (index >= end) { return false; }
    if (kindAt(index) == ast::SyntaxKind::Period) { return true; }
    return kindAt(index) == ast::SyntaxKind::ColonColon;
  }

  size_t modulePathSeparatorWidth(size_t index, size_t end) const {
    if (index < end && kindAt(index) == ast::SyntaxKind::Period) { return 1; }
    if (index < end && kindAt(index) == ast::SyntaxKind::ColonColon) { return 1; }
    return 0;
  }

  bool modulePathSeparatorPrecedesGroup(size_t index, size_t end) const {
    const size_t width = modulePathSeparatorWidth(index, end);
    return width > 0 && index + width < end && kindAt(index + width) == ast::SyntaxKind::LeftBrace;
  }

  size_t findModulePathEnd(size_t start, size_t end) const {
    size_t cursor = start;
    size_t lastSegmentEnd = start;
    bool expectSegment = true;

    while (cursor < end) {
      if (expectSegment) {
        if (!isAttributePathSegment(kindAt(cursor))) { break; }
        lastSegmentEnd = cursor + 1;
        ++cursor;
        expectSegment = false;
        continue;
      }

      if (!isModulePathSeparatorAt(cursor, end) || modulePathSeparatorPrecedesGroup(cursor, end)) {
        break;
      }

      cursor += modulePathSeparatorWidth(cursor, end);
      expectSegment = true;
    }

    return expectSegment ? lastSegmentEnd : cursor;
  }

  size_t findModuleSpecifierGroupOpen(size_t pathEnd, size_t end) const {
    if (pathEnd >= end) { return end; }
    if (kindAt(pathEnd) == ast::SyntaxKind::Period && pathEnd + 1 < end &&
        kindAt(pathEnd + 1) == ast::SyntaxKind::LeftBrace) {
      return pathEnd + 1;
    }
    if (pathEnd + 1 < end && kindAt(pathEnd) == ast::SyntaxKind::ColonColon &&
        kindAt(pathEnd + 1) == ast::SyntaxKind::LeftBrace) {
      return pathEnd + 1;
    }
    return end;
  }

  ast::NodeId makeImportSpecifier(ast::TreeBuilder& builder, size_t nameIndex, size_t aliasIndex,
                                  size_t end) const {
    ast::NodePayload payload;
    writeIdent(payload, ast::kImportSpecifierNameWord, internIdent(builder, nameIndex));
    if (aliasIndex < end) {
      writeIdent(payload, ast::kImportSpecifierAliasWord, internIdent(builder, aliasIndex));
    }
    return builder.makeNode(ast::SyntaxKind::ImportSpecifier, rangeFor(nameIndex, end), payload);
  }

  ast::NodeId makeExportSpecifier(ast::TreeBuilder& builder, size_t nameIndex, size_t aliasIndex,
                                  size_t end) const {
    ast::NodePayload payload;
    writeIdent(payload, ast::kExportSpecifierNameWord, internIdent(builder, nameIndex));
    if (aliasIndex < end) {
      writeIdent(payload, ast::kExportSpecifierAliasWord, internIdent(builder, aliasIndex));
    }
    return builder.makeNode(ast::SyntaxKind::ExportSpecifier, rangeFor(nameIndex, end), payload);
  }

  void recoverModuleSpecifier(TokenCursor& cursor, size_t end) const {
    while (cursor.position() < end && cursor.peek() != ast::SyntaxKind::Comma) { cursor.advance(); }
  }

  ast::NodeId parseImportSpecifier(ast::TreeBuilder& builder, TokenCursor& cursor,
                                   size_t end) const {
    const size_t start = cursor.position();
    if (start >= end) { return ast::NodeId(); }
    if (cursor.peek() != ast::SyntaxKind::Identifier) {
      diagnosticEngine.diagnose<diagnostics::DiagID::IdentifierExpected>(diagnosticLoc(start));
      recoverModuleSpecifier(cursor, end);
      return ast::NodeId();
    }

    const size_t nameIndex = cursor.position();
    cursor.advance();
    size_t nodeEnd = cursor.position();
    size_t aliasIndex = nodeEnd;

    if (cursor.position() < end && cursor.peek() == ast::SyntaxKind::AsKeyword) {
      cursor.advance();
      if (cursor.position() < end && cursor.peek() == ast::SyntaxKind::Identifier) {
        aliasIndex = cursor.position();
        cursor.advance();
        nodeEnd = cursor.position();
      } else {
        diagnosticEngine.diagnose<diagnostics::DiagID::IdentifierExpected>(
            diagnosticLoc(cursor.position()));
        nodeEnd = cursor.position();
      }
    }

    if (cursor.position() < end && cursor.peek() != ast::SyntaxKind::Comma) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(
          diagnosticLoc(cursor.position()), ","_zc);
      recoverModuleSpecifier(cursor, end);
    }

    return makeImportSpecifier(builder, nameIndex, aliasIndex, nodeEnd);
  }

  ast::NodeId parseExportSpecifier(ast::TreeBuilder& builder, TokenCursor& cursor,
                                   size_t end) const {
    const size_t start = cursor.position();
    if (start >= end) { return ast::NodeId(); }
    if (cursor.peek() != ast::SyntaxKind::Identifier) {
      diagnosticEngine.diagnose<diagnostics::DiagID::IdentifierExpected>(diagnosticLoc(start));
      recoverModuleSpecifier(cursor, end);
      return ast::NodeId();
    }

    const size_t nameIndex = cursor.position();
    cursor.advance();
    size_t nodeEnd = cursor.position();
    size_t aliasIndex = nodeEnd;

    if (cursor.position() < end && cursor.peek() == ast::SyntaxKind::AsKeyword) {
      cursor.advance();
      if (cursor.position() < end && cursor.peek() == ast::SyntaxKind::Identifier) {
        aliasIndex = cursor.position();
        cursor.advance();
        nodeEnd = cursor.position();
      } else {
        diagnosticEngine.diagnose<diagnostics::DiagID::IdentifierExpected>(
            diagnosticLoc(cursor.position()));
        nodeEnd = cursor.position();
      }
    }

    if (cursor.position() < end && cursor.peek() != ast::SyntaxKind::Comma) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(
          diagnosticLoc(cursor.position()), ","_zc);
      recoverModuleSpecifier(cursor, end);
    }

    return makeExportSpecifier(builder, nameIndex, aliasIndex, nodeEnd);
  }

  zc::Vector<ast::NodeId> parseImportSpecifierList(ast::TreeBuilder& builder, size_t start,
                                                   size_t end) const {
    zc::Vector<ast::NodeId> specifiers;
    TokenCursor cursor = tokenCursorAt(start);
    while (cursor.position() < end) {
      if (cursor.peek() == ast::SyntaxKind::Comma) {
        cursor.advance();
        continue;
      }

      const size_t specifierStart = cursor.position();
      addNodeIfPresent(specifiers, parseImportSpecifier(builder, cursor, end));
      if (cursor.position() <= specifierStart) { cursor.moveTo(specifierStart + 1); }
      if (cursor.position() < end && cursor.peek() == ast::SyntaxKind::Comma) { cursor.advance(); }
    }
    return specifiers;
  }

  zc::Vector<ast::NodeId> parseExportSpecifierList(ast::TreeBuilder& builder, size_t start,
                                                   size_t end) const {
    zc::Vector<ast::NodeId> specifiers;
    TokenCursor cursor = tokenCursorAt(start);
    while (cursor.position() < end) {
      if (cursor.peek() == ast::SyntaxKind::Comma) {
        cursor.advance();
        continue;
      }

      const size_t specifierStart = cursor.position();
      addNodeIfPresent(specifiers, parseExportSpecifier(builder, cursor, end));
      if (cursor.position() <= specifierStart) { cursor.moveTo(specifierStart + 1); }
      if (cursor.position() < end && cursor.peek() == ast::SyntaxKind::Comma) { cursor.advance(); }
    }
    return specifiers;
  }

  ast::NodeId makeAttributePath(ast::TreeBuilder& builder, size_t start, size_t end) const {
    zc::Vector<ast::IdentId> segments;
    for (size_t index = start; index < end; ++index) {
      if (isAttributePathSegment(kindAt(index))) { segments.add(internIdent(builder, index)); }
    }

    ast::NodePayload payload;
    writeIdentList(payload, ast::kAttributePathSegmentsFirstWord,
                   ast::kAttributePathSegmentsSizeWord, builder.makeIdentList(segments.asPtr()));
    return builder.makeNode(ast::SyntaxKind::AttributePath, rangeFor(start, end), payload);
  }

  size_t findAttributePathEnd(size_t start, size_t end) const {
    size_t cursor = start;
    bool expectSegment = true;
    while (cursor < end) {
      if (expectSegment) {
        if (!isAttributePathSegment(kindAt(cursor))) { break; }
        expectSegment = false;
        ++cursor;
        continue;
      }

      if (kindAt(cursor) == ast::SyntaxKind::ColonColon) {
        ++cursor;
        expectSegment = true;
        continue;
      }
      break;
    }
    return cursor;
  }

  uint32_t attributePathSegmentCount(size_t start, size_t end) const {
    uint32_t count = 0;
    for (size_t index = start; index < end; ++index) {
      if (isAttributePathSegment(kindAt(index))) { ++count; }
    }
    return count;
  }

  bool isWhitelistedBareAttribute(size_t start, size_t end) const {
    if (attributePathSegmentCount(start, end) != 1) { return false; }
    zc::StringPtr text = tokenAt(start).getValue();
    if (text.size() == 0) { text = tokenLabel(tokenAt(start)); }
    return text == "inline"_zc || text == "deprecated"_zc || text == "cold"_zc || text == "repr"_zc;
  }

  void diagnoseImportPathSyntax(size_t clauseStart, size_t clauseEnd, size_t pathEnd,
                                size_t groupOpen) const {
    const size_t period = findTopLevelToken(clauseStart, clauseEnd, ast::SyntaxKind::Period);
    if (period < clauseEnd) {
      diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
          tokenAt(period).getLocation());
      return;
    }

    const size_t range = findTopLevelToken(clauseStart, clauseEnd, ast::SyntaxKind::DotDotDot);
    if (range < clauseEnd) {
      diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
          tokenAt(range).getLocation());
      return;
    }

    if (pathEnd < clauseEnd && kindAt(pathEnd) != ast::SyntaxKind::AsKeyword &&
        !(groupOpen < clauseEnd && pathEnd + 1 == groupOpen)) {
      diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
          tokenAt(pathEnd).getLocation());
      return;
    }

    if (groupOpen >= clauseEnd && attributePathSegmentCount(clauseStart, pathEnd) < 2) {
      diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
          tokenAt(clauseStart).getLocation());
    }
  }

  bool isZomCfgAttributePath(size_t start, size_t end) const {
    if (attributePathSegmentCount(start, end) != 2) { return false; }

    bool sawZom = false;
    for (size_t index = start; index < end; ++index) {
      if (!isAttributePathSegment(kindAt(index))) { continue; }
      zc::StringPtr text = tokenAt(index).getValue();
      if (text.size() == 0) { text = tokenLabel(tokenAt(index)); }
      if (!sawZom) {
        if (text != "zom"_zc) { return false; }
        sawZom = true;
      } else {
        return text == "cfg"_zc;
      }
    }
    return false;
  }

  bool containsUnmodeledRangeOperator(size_t start, size_t end) const {
    for (size_t index = start; index < end; ++index) {
      if (kindAt(index) == ast::SyntaxKind::DotDotDot) { return true; }
      if (index + 1 < end && kindAt(index) == ast::SyntaxKind::Period &&
          kindAt(index + 1) == ast::SyntaxKind::Period) {
        return true;
      }
    }
    return false;
  }

  void diagnoseCfgAttributeArgs(size_t start, size_t end) const {
    for (size_t index = start; index < end; ++index) {
      const ast::SyntaxKind kind = kindAt(index);
      if (kind == ast::SyntaxKind::Equals || kind == ast::SyntaxKind::ExclamationEquals ||
          kind == ast::SyntaxKind::LessThan || kind == ast::SyntaxKind::LessThanEquals ||
          kind == ast::SyntaxKind::GreaterThan || kind == ast::SyntaxKind::GreaterThanEquals) {
        if (index + 1 >= end || kindAt(index + 1) != ast::SyntaxKind::StringLiteral) {
          diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(index + 1),
                                                                        "string literal"_zc);
          continue;
        }
        if (tokenAt(index + 1).getValue().size() == 0) {
          diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(
              tokenAt(index + 1).getLocation(), "non-empty string literal"_zc);
        }
      }
    }
  }

  ast::NodeId parseAttribute(ast::TreeBuilder& builder, size_t start, size_t end) const {
    size_t cursor = start;
    while (cursor < end && kindAt(cursor) == ast::SyntaxKind::Comma) { ++cursor; }
    if (cursor >= end) { return ast::NodeId(); }

    const size_t pathEnd = findAttributePathEnd(cursor, end);
    if (pathEnd == cursor) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(cursor),
                                                                    "attribute path"_zc);
      return ast::NodeId();
    }
    if (pathEnd < end && kindAt(pathEnd) == ast::SyntaxKind::Period) {
      diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
          tokenAt(pathEnd).getLocation());
    }
    if (attributePathSegmentCount(cursor, pathEnd) < 2 &&
        !isWhitelistedBareAttribute(cursor, pathEnd)) {
      diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
          tokenAt(cursor).getLocation());
    }

    zc::Vector<ast::NodeId> args;
    if (pathEnd < end && kindAt(pathEnd) == ast::SyntaxKind::LeftParen) {
      const size_t closeParen = findMatchingRightParen(pathEnd, end);
      const size_t argsEnd = closeParen < end ? closeParen : end;
      if (isZomCfgAttributePath(cursor, pathEnd)) {
        diagnoseCfgAttributeArgs(pathEnd + 1, argsEnd);
      }
      TokenCursor argCursor = tokenCursorAt(pathEnd + 1);
      while (argCursor.position() < argsEnd) {
        const size_t argStart = argCursor.position();
        const size_t argEnd = consumeCommaDelimitedItem(argCursor, argsEnd);
        if (argStart < argEnd && !containsUnmodeledRangeOperator(argStart, argEnd)) {
          addNodeIfPresent(args, parseExpressionRange(builder, argStart, argEnd));
        }
        if (argCursor.position() < argsEnd && argCursor.peek() == ast::SyntaxKind::Comma) {
          argCursor.advance();
        }
      }
    } else if (pathEnd < end && kindAt(pathEnd) == ast::SyntaxKind::Equals && pathEnd + 1 < end) {
      addNodeIfPresent(args, parseExpressionRange(builder, pathEnd + 1, end));
    }

    ast::NodePayload payload;
    writeNode(payload, ast::kAttributePathWord, makeAttributePath(builder, cursor, pathEnd));
    writeNodeList(payload, ast::kAttributeArgsFirstWord, ast::kAttributeArgsSizeWord,
                  builder.makeList(args.asPtr()));
    return builder.makeNode(ast::SyntaxKind::Attribute, rangeFor(start, end), payload);
  }

  ast::NodeId parseOuterAttributeList(ast::TreeBuilder& builder, size_t start, size_t end) const {
    zc::Vector<ast::NodeId> attrs;
    size_t cursor = start;
    while (isOuterAttributeStart(cursor, end)) {
      const size_t closeBracket = findMatchingRightBracket(cursor + 1, end);
      if (closeBracket >= end) { break; }

      TokenCursor itemCursor = tokenCursorAt(cursor + 2);
      if (itemCursor.position() >= closeBracket) {
        diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(
            diagnosticLoc(itemCursor.position()), "attribute path"_zc);
      }
      while (itemCursor.position() < closeBracket) {
        const size_t itemStart = itemCursor.position();
        const size_t itemEnd = consumeCommaDelimitedItem(itemCursor, closeBracket);
        if (itemStart < itemEnd) {
          addNodeIfPresent(attrs, parseAttribute(builder, itemStart, itemEnd));
        }
        if (itemCursor.position() < closeBracket && itemCursor.peek() == ast::SyntaxKind::Comma) {
          itemCursor.advance();
        }
      }
      cursor = closeBracket + 1;
    }

    if (attrs.empty()) { return ast::NodeId(); }

    ast::NodePayload payload;
    writeNodeList(payload, ast::kAttributeListAttrsFirstWord, ast::kAttributeListAttrsSizeWord,
                  builder.makeList(attrs.asPtr()));
    return builder.makeNode(ast::SyntaxKind::AttributeList, rangeFor(start, cursor), payload);
  }

  bool outerAttributePrefixContainsZomCfg(size_t start, size_t end) const {
    size_t cursor = start;
    while (isOuterAttributeStart(cursor, end)) {
      const size_t closeBracket = findMatchingRightBracket(cursor + 1, end);
      if (closeBracket >= end) { break; }

      TokenCursor itemCursor = tokenCursorAt(cursor + 2);
      while (itemCursor.position() < closeBracket) {
        const size_t itemStart = itemCursor.position();
        const size_t itemEnd = consumeCommaDelimitedItem(itemCursor, closeBracket);
        const size_t pathEnd = findAttributePathEnd(itemStart, itemEnd);
        if (pathEnd > itemStart && isZomCfgAttributePath(itemStart, pathEnd)) { return true; }
        if (itemCursor.position() < closeBracket && itemCursor.peek() == ast::SyntaxKind::Comma) {
          itemCursor.advance();
        }
      }
      cursor = closeBracket + 1;
    }
    return false;
  }

  ast::NodeId makeStatementListItem(ast::TreeBuilder& builder, ast::NodeId item,
                                    source::SourceRange range,
                                    ast::NodeId attrs = ast::NodeId()) const {
    ast::NodePayload payload;
    writeNode(payload, ast::kStatementListItemItemWord, item);
    writeNode(payload, ast::kStatementListItemAttrsWord, attrs);
    return builder.makeNode(ast::SyntaxKind::StatementListItem, zc::mv(range), payload);
  }

  void emitUnexpected(const lexer::Token& where) const {
    diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(where.getLocation());
  }

  source::SourceLoc diagnosticLoc(size_t index) const { return context.diagnosticLoc(index); }

  void diagnoseExpressionExpected(size_t index) const {
    diagnosticEngine.diagnose<diagnostics::DiagID::ExpressionExpected>(diagnosticLoc(index));
  }

  bool modifierGroupContains(size_t start, size_t end, ast::SyntaxKind needle,
                             size_t& found) const {
    for (size_t cursor = start; cursor < end; ++cursor) {
      if (kindAt(cursor) == needle) {
        found = cursor;
        return true;
      }
    }
    return false;
  }

  void diagnoseDeclarationModifierGroup(size_t start, size_t end) const {
    for (size_t cursor = start; cursor < end; ++cursor) {
      for (size_t previous = start; previous < cursor; ++previous) {
        if (kindAt(previous) == kindAt(cursor)) {
          diagnosticEngine.diagnose<diagnostics::DiagID::DuplicateDeclarationModifier>(
              tokenAt(cursor).getLocation(), tokenLabel(tokenAt(cursor)));
          break;
        }
      }
    }

    size_t abstractIndex = end;
    size_t staticIndex = end;
    size_t mutatingIndex = end;
    const bool hasAbstract =
        modifierGroupContains(start, end, ast::SyntaxKind::AbstractKeyword, abstractIndex);
    const bool hasStatic =
        modifierGroupContains(start, end, ast::SyntaxKind::StaticKeyword, staticIndex);
    const bool hasMutating =
        modifierGroupContains(start, end, ast::SyntaxKind::MutatingKeyword, mutatingIndex);

    if (hasAbstract && hasStatic) {
      diagnosticEngine.diagnose<diagnostics::DiagID::IncompatibleDeclarationModifiers>(
          tokenAt(staticIndex).getLocation(), tokenLabel(tokenAt(abstractIndex)),
          tokenLabel(tokenAt(staticIndex)));
    }
    if (hasStatic && hasMutating) {
      diagnosticEngine.diagnose<diagnostics::DiagID::IncompatibleDeclarationModifiers>(
          tokenAt(mutatingIndex).getLocation(), tokenLabel(tokenAt(staticIndex)),
          tokenLabel(tokenAt(mutatingIndex)));
    }
  }

  bool diagnoseUnsupportedVarianceInTypeParameters(size_t openAngle, size_t closeAngle) const {
    if (openAngle >= closeAngle || closeAngle >= tokenCountWithoutEof()) { return false; }

    bool found = false;
    int32_t angleDepth = 0;
    bool atParameterStart = true;
    for (size_t cursor = openAngle + 1; cursor < closeAngle; ++cursor) {
      const ast::SyntaxKind kind = kindAt(cursor);

      if (kind == ast::SyntaxKind::LessThan) {
        ++angleDepth;
        atParameterStart = false;
        continue;
      }

      if (kind == ast::SyntaxKind::GreaterThan) {
        if (angleDepth > 0) { --angleDepth; }
        atParameterStart = false;
        continue;
      }

      if (angleDepth == 0) {
        if (kind == ast::SyntaxKind::Comma) {
          atParameterStart = true;
          continue;
        }

        if (atParameterStart &&
            (kind == ast::SyntaxKind::InKeyword || kind == ast::SyntaxKind::OutKeyword)) {
          diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
              tokenAt(cursor).getLocation());
          found = true;
        }
      }

      atParameterStart = false;
    }

    return found;
  }

  bool diagnoseTypeParameterListSyntax(size_t openAngle, size_t closeAngle) const {
    if (openAngle >= tokenCountWithoutEof() || kindAt(openAngle) != ast::SyntaxKind::LessThan) {
      return false;
    }

    bool found = false;
    if (closeAngle >= tokenCountWithoutEof()) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(
          tokenAt(openAngle).getLocation(), ">"_zc);
      return true;
    }

    const int32_t closeCount = typeAngleCloseCount(kindAt(closeAngle));
    if (closeCount != 1) {
      diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
          tokenAt(closeAngle).getLocation());
      found = true;
    }

    if (openAngle + 1 == closeAngle) {
      diagnosticEngine.diagnose<diagnostics::DiagID::TypeParameterDeclarationExpected>(
          diagnosticLoc(openAngle + 1));
      found = true;
    }

    int32_t angleDepth = 0;
    for (size_t cursor = openAngle + 1; cursor < closeAngle; ++cursor) {
      const ast::SyntaxKind kind = kindAt(cursor);
      if (kind == ast::SyntaxKind::LessThan) {
        ++angleDepth;
        continue;
      }
      const int32_t nestedCloseCount = typeAngleCloseCount(kind);
      if (nestedCloseCount > 0) {
        angleDepth = nestedCloseCount >= angleDepth ? 0 : angleDepth - nestedCloseCount;
        continue;
      }
      if (angleDepth == 0 && kind == ast::SyntaxKind::ExtendsKeyword) {
        diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
            tokenAt(cursor).getLocation());
        found = true;
      }
    }

    return found;
  }

  void diagnoseDeclarationTypeParameterSyntax(size_t afterName, size_t limit) const {
    if (afterName >= limit || kindAt(afterName) != ast::SyntaxKind::LessThan) { return; }
    const size_t closeAngle = findMatchingAngleClose(afterName, limit);
    if (diagnoseTypeParameterListSyntax(afterName, closeAngle)) { return; }
    diagnoseUnsupportedVarianceInTypeParameters(afterName, closeAngle);
  }

  bool diagnoseTypeArgumentListSyntax(size_t openAngle, size_t closeAngle, size_t limit) const {
    if (openAngle >= limit || kindAt(openAngle) != ast::SyntaxKind::LessThan) { return false; }

    bool found = false;
    if (closeAngle >= limit) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(
          tokenAt(openAngle).getLocation(), ">"_zc);
      return true;
    }

    if (openAngle + 1 == closeAngle) {
      diagnosticEngine.diagnose<diagnostics::DiagID::TypeArgumentExpected>(
          diagnosticLoc(openAngle + 1));
      found = true;
    }

    int32_t depth = 1;
    for (size_t cursor = openAngle + 1; cursor < closeAngle; ++cursor) {
      const ast::SyntaxKind kind = kindAt(cursor);
      if (kind == ast::SyntaxKind::LessThan) {
        ++depth;
        if (depth > 3) {
          diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
              tokenAt(cursor).getLocation());
          found = true;
        }
        continue;
      }

      const int32_t closeCount = typeAngleCloseCount(kind);
      if (closeCount > 0 && depth > 1) { depth = closeCount >= depth ? 1 : depth - closeCount; }
    }

    return found;
  }

  ast::NodeId parseRequiredExpression(ast::TreeBuilder& builder, size_t start, size_t end) const {
    const ast::NodeId expr = parseExpressionRange(builder, start, end);
    if (!expr) { diagnoseExpressionExpected(start); }
    return expr;
  }

  bool requireTrailingSemicolon(size_t start, size_t end) const {
    if (start < end && kindAt(end - 1) == ast::SyntaxKind::Semicolon) { return true; }
    diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(end), ";"_zc);
    return false;
  }

  bool isInterfaceElementHead(size_t index, int32_t interfaceBodyDepth) const {
    if (interfaceBodyDepth < 0 || index == 0) { return false; }

    for (size_t cursor = index; cursor > 0;) {
      --cursor;
      const ast::SyntaxKind kind = kindAt(cursor);
      if (kind == ast::SyntaxKind::LeftBrace || kind == ast::SyntaxKind::Semicolon) { return true; }
      if (isInterfaceModifier(kind)) { continue; }
      return false;
    }

    return false;
  }

  bool isInterfaceMethodInitializer(size_t index, int32_t interfaceBodyDepth) const {
    if (interfaceBodyDepth < 0 || kindAt(index) != ast::SyntaxKind::Equals) { return false; }

    for (size_t cursor = index; cursor > 0;) {
      --cursor;
      const ast::SyntaxKind kind = kindAt(cursor);
      if (kind == ast::SyntaxKind::FunKeyword) { return true; }
      if (kind == ast::SyntaxKind::LeftBrace || kind == ast::SyntaxKind::Semicolon ||
          kind == ast::SyntaxKind::RightBrace) {
        return false;
      }
    }

    return false;
  }

  bool followsFieldTypeColonWithoutSemicolon(size_t index) const {
    const ast::SyntaxKind kind = kindAt(index);
    return kind == ast::SyntaxKind::LeftParen || kind == ast::SyntaxKind::LeftBracket ||
           kind == ast::SyntaxKind::LeftBrace || isPrimitiveTypeKeyword(kind);
  }

  size_t consumeMemberBoundary(size_t start, size_t limit) const {
    const ast::SyntaxKind head = kindAt(start);
    const bool bodyBearingHead =
        head == ast::SyntaxKind::FunKeyword || head == ast::SyntaxKind::GetKeyword ||
        head == ast::SyntaxKind::SetKeyword || head == ast::SyntaxKind::InitKeyword ||
        head == ast::SyntaxKind::DeinitKeyword;
    int32_t angleDepth = 0;
    bool sawFieldColon = false;
    bool sawEquals = false;
    for (size_t cursor = start; cursor < limit;) {
      const ast::SyntaxKind kind = kindAt(cursor);
      if (angleDepth == 0) {
        if (kind == ast::SyntaxKind::Semicolon || kind == ast::SyntaxKind::Comma) {
          return cursor + 1;
        }
        if (kind == ast::SyntaxKind::Colon) { sawFieldColon = true; }
        if (kind == ast::SyntaxKind::Equals) { sawEquals = true; }
        if (kind == ast::SyntaxKind::LeftParen) {
          const size_t closeParen = findMatchingRightParen(cursor, limit);
          cursor = closeParen < limit ? closeParen + 1 : limit;
          continue;
        }
        if (kind == ast::SyntaxKind::LeftBracket) {
          const size_t closeBracket = findMatchingRightBracket(cursor, limit);
          cursor = closeBracket < limit ? closeBracket + 1 : limit;
          continue;
        }
        if (kind == ast::SyntaxKind::LeftBrace) {
          const size_t bodyEnd = consumeBracedBodyEnd(cursor, limit);
          const bool fieldAccessorBody = head == ast::SyntaxKind::Identifier && sawFieldColon &&
                                         !sawEquals && cursor > start &&
                                         kindAt(cursor - 1) != ast::SyntaxKind::Colon;
          if (bodyBearingHead || fieldAccessorBody) { return bodyEnd; }
          cursor = bodyEnd;
          continue;
        }
      }

      if (kind == ast::SyntaxKind::LessThan) {
        ++angleDepth;
      } else {
        const int32_t closeCount = typeAngleCloseCount(kind);
        if (closeCount > 0 && angleDepth > 0) {
          angleDepth = closeCount >= angleDepth ? 0 : angleDepth - closeCount;
        }
      }
      ++cursor;
    }
    return limit;
  }

  void diagnoseMissingFieldMemberSemicolon(size_t start, size_t end) const {
    if (start >= end || kindAt(start) != ast::SyntaxKind::Identifier) { return; }

    const size_t colon = findTopLevelToken(start + 1, end, ast::SyntaxKind::Colon);
    if (colon >= end) { return; }

    int32_t parenDepth = 0;
    int32_t bracketDepth = 0;
    int32_t braceDepth = 0;
    int32_t angleDepth = 0;
    for (size_t index = colon + 1; index + 1 < end; ++index) {
      const ast::SyntaxKind kind = kindAt(index);

      if (parenDepth == 0 && bracketDepth == 0 && braceDepth == 0 && angleDepth == 0 &&
          kind == ast::SyntaxKind::Identifier && kindAt(index + 1) == ast::SyntaxKind::Colon) {
        diagnosticEngine.diagnose<diagnostics::DiagID::MissingSemicolon>(
            tokenAt(index).getLocation(), tokenLabel(tokenAt(index)));
        return;
      }

      switch (kind) {
        case ast::SyntaxKind::LeftParen:
          ++parenDepth;
          break;
        case ast::SyntaxKind::RightParen:
          if (parenDepth > 0) { --parenDepth; }
          break;
        case ast::SyntaxKind::LeftBracket:
          ++bracketDepth;
          break;
        case ast::SyntaxKind::RightBracket:
          if (bracketDepth > 0) { --bracketDepth; }
          break;
        case ast::SyntaxKind::LeftBrace:
          ++braceDepth;
          break;
        case ast::SyntaxKind::RightBrace:
          if (braceDepth > 0) { --braceDepth; }
          break;
        case ast::SyntaxKind::LessThan:
          ++angleDepth;
          break;
        default: {
          const int32_t closeCount = typeAngleCloseCount(kind);
          if (closeCount > 0 && angleDepth > 0) {
            angleDepth = closeCount >= angleDepth ? 0 : angleDepth - closeCount;
          }
          break;
        }
      }
    }
  }

  void diagnoseNamedTypeBody(size_t bodyOpen, size_t bodyClose, ast::SyntaxKind kind) const {
    bool previousClassMemberWasGetter = false;
    size_t cursor = bodyOpen + 1;
    while (cursor < bodyClose) {
      const size_t memberStart = cursor;
      cursor = skipOuterAttributePrefix(cursor, bodyClose);

      const size_t modifiersStart = cursor;
      while (cursor < bodyClose && isDeclarationModifier(kindAt(cursor))) { ++cursor; }
      const size_t modifiersEnd = cursor;
      if (cursor >= bodyClose) { break; }

      const ast::SyntaxKind head = kindAt(cursor);
      const size_t memberEnd = consumeMemberBoundary(cursor, bodyClose);
      const size_t semi = findTopLevelToken(cursor, memberEnd, ast::SyntaxKind::Semicolon);
      const size_t body = findTopLevelToken(cursor, memberEnd, ast::SyntaxKind::LeftBrace);

      if (modifiersStart < modifiersEnd) {
        diagnoseDeclarationModifierGroup(modifiersStart, modifiersEnd);
      }

      if (isNamedTypeDeclarationHead(head)) {
        diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
            tokenAt(cursor).getLocation());
      }

      if ((kind == ast::SyntaxKind::ClassDecl || kind == ast::SyntaxKind::StructDecl) &&
          head == ast::SyntaxKind::Identifier) {
        diagnoseMissingFieldMemberSemicolon(cursor, memberEnd);
      }

      if ((kind == ast::SyntaxKind::ClassDecl || kind == ast::SyntaxKind::StructDecl) &&
          head == ast::SyntaxKind::FunKeyword) {
        size_t abstractIndex = modifiersEnd;
        if (modifierGroupContains(modifiersStart, modifiersEnd, ast::SyntaxKind::AbstractKeyword,
                                  abstractIndex) &&
            body < memberEnd && (semi >= memberEnd || body < semi)) {
          diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
              tokenAt(body).getLocation());
        }
      }

      if (kind == ast::SyntaxKind::ClassDecl) {
        if (head == ast::SyntaxKind::SetKeyword && !previousClassMemberWasGetter) {
          diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
              tokenAt(cursor).getLocation());
        }
        previousClassMemberWasGetter = head == ast::SyntaxKind::GetKeyword;
      }

      if (kind == ast::SyntaxKind::InterfaceDecl) {
        if (head == ast::SyntaxKind::InitKeyword || head == ast::SyntaxKind::DeinitKeyword) {
          diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
              tokenAt(cursor).getLocation());
        }

        if ((head == ast::SyntaxKind::FunKeyword || head == ast::SyntaxKind::GetKeyword ||
             head == ast::SyntaxKind::SetKeyword) &&
            body < memberEnd && (semi >= memberEnd || body < semi)) {
          diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(tokenAt(body).getLocation(),
                                                                        ";"_zc);
        }

        if (head == ast::SyntaxKind::TypeKeyword && semi >= memberEnd) {
          diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(memberEnd),
                                                                        ";"_zc);
        }
      }

      if (kind == ast::SyntaxKind::EnumDeclaration) {
        if (body < memberEnd) {
          diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
              tokenAt(body).getLocation());
        }
        const size_t equals = findTopLevelToken(cursor, memberEnd, ast::SyntaxKind::Equals);
        if (equals < memberEnd &&
            (equals + 1 >= memberEnd || kindAt(equals + 1) == ast::SyntaxKind::Comma ||
             kindAt(equals + 1) == ast::SyntaxKind::RightBrace)) {
          diagnosticEngine.diagnose<diagnostics::DiagID::ExpressionExpected>(
              diagnosticLoc(equals + 1));
        }
      }

      cursor = memberEnd > memberStart ? memberEnd : memberStart + 1;
    }
  }

  bool looksLikeObjectLiteralExpression(size_t start, size_t end) const {
    if (!rangeIsWrapped(start, end, ast::SyntaxKind::LeftBrace, ast::SyntaxKind::RightBrace)) {
      return false;
    }
    return findTopLevelToken(start + 1, end - 1, ast::SyntaxKind::Colon) < end - 1 ||
           findTopLevelToken(start + 1, end - 1, ast::SyntaxKind::DotDotDot) < end - 1;
  }

  bool isStructLiteralTypeReference(size_t start, size_t end) const {
    if (start >= end) { return false; }

    size_t cursor = findTypePathEnd(start, end);
    if (cursor == start) { return false; }
    if (cursor == end) { return true; }

    if (kindAt(cursor) == ast::SyntaxKind::LessThan) {
      const size_t closeAngle = findMatchingAngleClose(cursor, end);
      return closeAngle + 1 == end;
    }

    return false;
  }

  size_t findTypePathEnd(size_t start, size_t end) const {
    bool expectSegment = true;
    size_t cursor = start;
    if (cursor < end && kindAt(cursor) == ast::SyntaxKind::ColonColon) { ++cursor; }

    for (; cursor < end; ++cursor) {
      const ast::SyntaxKind kind = kindAt(cursor);
      if (expectSegment) {
        if (kind != ast::SyntaxKind::Identifier) { return start; }
        expectSegment = false;
        continue;
      }

      if (kind == ast::SyntaxKind::Period) {
        expectSegment = true;
        continue;
      }
      if (kind == ast::SyntaxKind::ColonColon) {
        expectSegment = true;
        continue;
      }
      return cursor;
    }

    return expectSegment ? start : cursor;
  }

  size_t findStructLiteralBrace(size_t start, size_t end) const {
    const size_t brace = findTopLevelToken(start, end, ast::SyntaxKind::LeftBrace);
    if (brace <= start || brace >= end) { return end; }
    if (!rangeIsWrapped(brace, end, ast::SyntaxKind::LeftBrace, ast::SyntaxKind::RightBrace)) {
      return end;
    }
    return isStructLiteralTypeReference(start, brace) ? brace : end;
  }

  bool isDefinitelyNonLValueOperand(size_t start, size_t end) const {
    while (rangeIsWrapped(start, end, ast::SyntaxKind::LeftParen, ast::SyntaxKind::RightParen)) {
      ++start;
      --end;
    }
    return start < end && isLiteralExpressionToken(kindAt(start));
  }

  void diagnoseTokenPatterns() {
    int32_t braceDepth = 0;
    bool sawTopLevelBlock = false;
    bool waitingForInterfaceBody = false;
    int32_t interfaceBodyDepth = -1;
    int32_t typeLiteralBraceDepth = -1;
    int32_t bindingPatternBraceDepth = -1;
    int32_t matchArmPatternBraceDepth = -1;
    size_t macroTokenTreeEnd = 0;

    const size_t count = tokenCountWithoutEof();
    for (size_t i = 0; i < count; ++i) {
      const lexer::Token& current = tokenAt(i);
      const ast::SyntaxKind kind = current.getKind();
      const ast::SyntaxKind next = i + 1 < count ? kindAt(i + 1) : ast::SyntaxKind::EndOfFile;
      if (i >= macroTokenTreeEnd) { macroTokenTreeEnd = 0; }
      if (macroTokenTreeEnd == 0 && isMacroInvocationStart(i, count)) {
        macroTokenTreeEnd = findMacroInvocationEnd(i, count);
      }
      const bool insideMacroTokenTree = macroTokenTreeEnd > i;
      const bool insideInterfaceBody = interfaceBodyDepth >= 0 && braceDepth >= interfaceBodyDepth;
      const bool insideInterfaceTopLevel =
          interfaceBodyDepth >= 0 && braceDepth == interfaceBodyDepth;

      if (isDeclarationModifier(kind) && (i == 0 || !isDeclarationModifier(kindAt(i - 1)))) {
        size_t head = i + 1;
        while (head < count && isDeclarationModifier(kindAt(head))) { ++head; }
        if (head < count && isDeclarationHead(kindAt(head))) {
          diagnoseDeclarationModifierGroup(i, head);
        }
      }

      if (isUnsupportedStatementKeyword(kind)) { emitUnexpected(current); }

      if (kind == ast::SyntaxKind::InterfaceKeyword) { waitingForInterfaceBody = true; }

      if (kind == ast::SyntaxKind::ExportKeyword && next == ast::SyntaxKind::DefaultKeyword) {
        diagnosticEngine.diagnose<diagnostics::DiagID::DeclarationExpected>(current.getLocation());
      }

      if (kind == ast::SyntaxKind::LetKeyword && next == ast::SyntaxKind::Equals) {
        diagnosticEngine.diagnose<diagnostics::DiagID::VariableDeclarationExpected>(
            tokenAt(i + 1).getLocation());
      }

      if (kind == ast::SyntaxKind::AsKeyword && current.hasPrecedingLineBreak()) {
        diagnosticEngine.diagnose<diagnostics::DiagID::MissingSemicolon>(current.getLocation(),
                                                                         tokenLabel(current));
      }

      if (kind == ast::SyntaxKind::SuperKeyword && next != ast::SyntaxKind::Period &&
          next != ast::SyntaxKind::LeftParen && next != ast::SyntaxKind::LeftBracket) {
        diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
            current.getLocation());
      }

      if (kind == ast::SyntaxKind::Comma && next == ast::SyntaxKind::Comma) {
        diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
            tokenAt(i + 1).getLocation());
      }

      if (kind == ast::SyntaxKind::Question && next == ast::SyntaxKind::Colon) {
        diagnosticEngine.diagnose<diagnostics::DiagID::ExpressionExpected>(
            tokenAt(i + 1).getLocation());
      }

      if (kind == ast::SyntaxKind::Bar && next == ast::SyntaxKind::Semicolon) {
        diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(tokenAt(i + 1).getLocation());
      }

      if (isTemplateLiteralToken(kind) && i > 0 && canPrecedeTaggedTemplate(kindAt(i - 1))) {
        diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
            current.getLocation());
      }

      if (kind == ast::SyntaxKind::WhenKeyword &&
          findTopLevelToken(i + 1, count, ast::SyntaxKind::EqualsGreaterThan) < count) {
        matchArmPatternBraceDepth = braceDepth;
      }
      const bool insideMatchArmPattern = matchArmPatternBraceDepth >= 0;
      if (kind == ast::SyntaxKind::EqualsGreaterThan && matchArmPatternBraceDepth >= 0 &&
          braceDepth == matchArmPatternBraceDepth) {
        matchArmPatternBraceDepth = -1;
      }

      if (kind == ast::SyntaxKind::QuestionDot) {
        if (next == ast::SyntaxKind::Semicolon || next == ast::SyntaxKind::RightBracket ||
            next == ast::SyntaxKind::EndOfFile) {
          diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(current.getLocation(),
                                                                        "member"_zc);
        }
        if (next == ast::SyntaxKind::LeftBracket && i + 2 < count &&
            kindAt(i + 2) == ast::SyntaxKind::RightBracket) {
          diagnosticEngine.diagnose<diagnostics::DiagID::ExpressionExpected>(
              tokenAt(i + 2).getLocation());
        }
      }

      if (kind == ast::SyntaxKind::NewKeyword) {
        for (size_t j = i + 1; j < count; ++j) {
          const ast::SyntaxKind nested = kindAt(j);
          if (nested == ast::SyntaxKind::Semicolon || nested == ast::SyntaxKind::RightBrace) {
            break;
          }
          if (nested == ast::SyntaxKind::QuestionDot) {
            diagnosticEngine.diagnose<diagnostics::DiagID::InvalidOptionalChainFromNewExpression>(
                tokenAt(j).getLocation());
            break;
          }
        }
      }

      if (kind == ast::SyntaxKind::OfKeyword) {
        diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
            current.getLocation());
      }

      if (braceDepth > 0 && kind == ast::SyntaxKind::ModuleKeyword) {
        diagnosticEngine.diagnose<diagnostics::DiagID::ModuleDeclarationMustBeFirst>(
            current.getLocation());
      }

      if (insideInterfaceBody) {
        if (kind == ast::SyntaxKind::StringLiteral || kind == ast::SyntaxKind::IntegerLiteral ||
            kind == ast::SyntaxKind::FloatLiteral || kind == ast::SyntaxKind::CharacterLiteral) {
          if (next == ast::SyntaxKind::Colon) {
            diagnosticEngine.diagnose<diagnostics::DiagID::IdentifierExpected>(
                current.getLocation());
          }
        }

        if (kind == ast::SyntaxKind::LeftBracket) {
          for (size_t j = i + 1; j < count; ++j) {
            const ast::SyntaxKind nested = kindAt(j);
            if (nested == ast::SyntaxKind::Colon) {
              diagnosticEngine.diagnose<diagnostics::DiagID::IdentifierExpected>(
                  current.getLocation());
              break;
            }
            if (nested == ast::SyntaxKind::Semicolon || nested == ast::SyntaxKind::RightBrace) {
              break;
            }
          }
        }

        if (insideInterfaceTopLevel && isInterfaceElementHead(i, interfaceBodyDepth) &&
            kind == ast::SyntaxKind::Identifier && next == ast::SyntaxKind::LeftParen) {
          diagnosticEngine.diagnose<diagnostics::DiagID::PropertyOrSignatureExpected>(
              current.getLocation());
        }

        if (insideInterfaceTopLevel && isInterfaceElementHead(i, interfaceBodyDepth) &&
            kind == ast::SyntaxKind::ClassKeyword) {
          diagnosticEngine.diagnose<diagnostics::DiagID::PropertyOrSignatureExpected>(
              current.getLocation());
        }

        if (insideInterfaceTopLevel && kind == ast::SyntaxKind::LetKeyword &&
            next == ast::SyntaxKind::Colon) {
          diagnosticEngine.diagnose<diagnostics::DiagID::IdentifierExpected>(
              tokenAt(i + 1).getLocation());
        }

        if (insideInterfaceTopLevel && kind == ast::SyntaxKind::FunKeyword) {
          for (size_t j = i + 1; j < count; ++j) {
            const ast::SyntaxKind nested = kindAt(j);
            if (nested == ast::SyntaxKind::LeftParen || nested == ast::SyntaxKind::Semicolon ||
                nested == ast::SyntaxKind::LeftBrace || nested == ast::SyntaxKind::RightBrace) {
              break;
            }
            if (nested == ast::SyntaxKind::Arrow) {
              diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(
                  tokenAt(j).getLocation(), "("_zc);
              break;
            }
          }
        }

        if (insideInterfaceTopLevel && kind == ast::SyntaxKind::LetKeyword && i + 2 < count &&
            next == ast::SyntaxKind::Identifier && kindAt(i + 2) == ast::SyntaxKind::Arrow) {
          diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(
              tokenAt(i + 2).getLocation(), ":"_zc);
        }

        if (insideInterfaceTopLevel && isInterfaceMethodInitializer(i, interfaceBodyDepth)) {
          diagnosticEngine
              .diagnose<diagnostics::DiagID::InterfaceMethodSignatureInitializerNotAllowed>(
                  current.getLocation());
        }
      }

      if (!insideMacroTokenTree && braceDepth > 0 && isInvalidObjectLiteralPropertyName(kind) &&
          next == ast::SyntaxKind::Colon) {
        diagnosticEngine.diagnose<diagnostics::DiagID::ExceptedIdentifier>(current.getLocation(),
                                                                           tokenLabel(current));
      }

      if (kind == ast::SyntaxKind::LeftBrace && braceDepth == 0) {
        sawTopLevelBlock = true;
        if (i > 0 && kindAt(i - 1) == ast::SyntaxKind::Colon) {
          typeLiteralBraceDepth = braceDepth + 1;
        }
        if (i > 0 && (kindAt(i - 1) == ast::SyntaxKind::LetKeyword ||
                      kindAt(i - 1) == ast::SyntaxKind::ConstKeyword)) {
          bindingPatternBraceDepth = braceDepth + 1;
        }
      }
      if (kind == ast::SyntaxKind::LeftBrace) {
        ++braceDepth;
        if (waitingForInterfaceBody) {
          interfaceBodyDepth = braceDepth;
          waitingForInterfaceBody = false;
        }
      }
      if (kind == ast::SyntaxKind::RightBrace) {
        if (braceDepth == 0) {
          diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
              current.getLocation());
        } else {
          const bool closesTypeLiteral = braceDepth == typeLiteralBraceDepth;
          const bool closesBindingPattern = braceDepth == bindingPatternBraceDepth;
          if (braceDepth == interfaceBodyDepth) { interfaceBodyDepth = -1; }
          if (closesTypeLiteral) { typeLiteralBraceDepth = -1; }
          if (closesBindingPattern) { bindingPatternBraceDepth = -1; }
          --braceDepth;
          if (sawTopLevelBlock && braceDepth == 0 && next == ast::SyntaxKind::Equals &&
              !closesTypeLiteral && !closesBindingPattern) {
            diagnosticEngine
                .diagnose<diagnostics::DiagID::DeclarationOrStatementExpectedAfterBlock>(
                    tokenAt(i + 1).getLocation());
          }
        }
      }

      if (!insideMacroTokenTree && !insideMatchArmPattern && braceDepth > 0 &&
          kind == ast::SyntaxKind::Identifier && i + 3 < count &&
          kindAt(i + 1) == ast::SyntaxKind::Colon && kindAt(i + 2) != ast::SyntaxKind::Semicolon &&
          kindAt(i + 2) != ast::SyntaxKind::RightBrace &&
          !followsFieldTypeColonWithoutSemicolon(i + 2) &&
          kindAt(i + 3) == ast::SyntaxKind::Identifier) {
        diagnosticEngine.diagnose<diagnostics::DiagID::MissingSemicolon>(
            tokenAt(i + 3).getLocation(), tokenLabel(tokenAt(i + 3)));
      }
    }

    if (braceDepth > 0 && count > 0) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(
          tokenAt(count - 1).getLocation(), "}"_zc);
    }
    if (waitingForInterfaceBody && count > 0) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(
          tokenAt(count - 1).getLocation(), "{"_zc);
    }
  }

  size_t effectiveStatementStart(size_t start, size_t end) const {
    size_t head = skipOuterAttributePrefix(start, end);
    while (head < end && isDeclarationModifier(kindAt(head))) { ++head; }
    return head < end ? head : start;
  }

  struct TypeParseResult {
    ast::NodeId node;
    size_t next = 0;
  };

  size_t findMatchingAngleClose(size_t openIndex, size_t limit) const {
    if (openIndex >= limit || kindAt(openIndex) != ast::SyntaxKind::LessThan) { return limit; }

    int32_t depth = 0;
    for (size_t index = openIndex; index < limit; ++index) {
      const ast::SyntaxKind kind = kindAt(index);
      if (kind == ast::SyntaxKind::LessThan) { ++depth; }
      const int32_t closeCount = typeAngleCloseCount(kind);
      if (closeCount > 0) {
        if (closeCount >= depth) { return index; }
        depth -= closeCount;
      }
    }
    return limit;
  }

  size_t functionTypeParameterTypeStart(TokenCursor& cursor, size_t limit) const {
    const TokenCursor::Mark mark = cursor.mark();
    int32_t parenDepth = 0;
    int32_t bracketDepth = 0;
    int32_t braceDepth = 0;
    int32_t angleDepth = 0;

    while (cursor.position() < limit) {
      const ast::SyntaxKind kind = cursor.peek();
      if (parenDepth == 0 && bracketDepth == 0 && braceDepth == 0 && angleDepth == 0) {
        if (kind == ast::SyntaxKind::Colon) {
          cursor.advance();
          return cursor.position();
        }
        if (kind == ast::SyntaxKind::Comma) {
          cursor.rewind(mark);
          return mark;
        }
      }

      if (kind == ast::SyntaxKind::LeftParen) {
        ++parenDepth;
      } else if (kind == ast::SyntaxKind::RightParen) {
        if (parenDepth > 0) { --parenDepth; }
      } else if (kind == ast::SyntaxKind::LeftBracket) {
        ++bracketDepth;
      } else if (kind == ast::SyntaxKind::RightBracket) {
        if (bracketDepth > 0) { --bracketDepth; }
      } else if (kind == ast::SyntaxKind::LeftBrace) {
        ++braceDepth;
      } else if (kind == ast::SyntaxKind::RightBrace) {
        if (braceDepth > 0) { --braceDepth; }
      } else if (kind == ast::SyntaxKind::LessThan) {
        ++angleDepth;
      } else {
        const int32_t closeCount = typeAngleCloseCount(kind);
        if (closeCount > 0 && angleDepth > 0) {
          angleDepth = closeCount >= angleDepth ? 0 : angleDepth - closeCount;
        }
      }
      cursor.advance();
    }

    cursor.rewind(mark);
    return mark;
  }

  ast::NodeList parseFunctionTypeParameters(ast::TreeBuilder& builder, size_t start,
                                            size_t end) const {
    zc::Vector<ast::NodeId> params;
    TokenCursor cursor = tokenCursorAt(start);
    while (cursor.position() < end) {
      if (cursor.peek() == ast::SyntaxKind::Comma) {
        cursor.advance();
        continue;
      }

      const size_t typeStart = functionTypeParameterTypeStart(cursor, end);
      TypeParseResult param = parseTypeExpression(builder, cursor, end);
      if (!param.node) {
        diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(typeStart));
        break;
      }
      params.add(param.node);

      if (cursor.position() >= end) { break; }
      if (cursor.peek() == ast::SyntaxKind::Comma) {
        cursor.advance();
        continue;
      }

      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(
          diagnosticLoc(cursor.position()), ","_zc);
      break;
    }
    return builder.makeList(params.asPtr());
  }

  ast::NodeId parseTupleTypeRange(ast::TreeBuilder& builder, size_t start, size_t end) const {
    zc::Vector<ast::NodeId> elems;
    bool hasTrailingComma = false;
    const size_t bodyEnd = end > start ? end - 1 : start;
    TokenCursor cursor = tokenCursorAt(start + 1);
    while (cursor.position() < bodyEnd) {
      if (cursor.peek() == ast::SyntaxKind::Comma) {
        if (cursor.position() + 1 >= bodyEnd && elems.size() > 0) { hasTrailingComma = true; }
        cursor.advance();
        continue;
      }

      if (cursor.peek() == ast::SyntaxKind::Identifier && cursor.position() + 1 < bodyEnd &&
          kindAt(cursor.position() + 1) == ast::SyntaxKind::Colon) {
        cursor.moveTo(cursor.position() + 2);
      }

      TypeParseResult elem = parseTypeExpression(builder, cursor, bodyEnd);
      if (!elem.node) {
        diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(
            diagnosticLoc(cursor.position()));
        return ast::NodeId();
      }
      elems.add(elem.node);

      if (cursor.position() >= bodyEnd) { break; }
      if (cursor.peek() == ast::SyntaxKind::Comma) {
        if (cursor.position() + 1 >= bodyEnd) { hasTrailingComma = true; }
        cursor.advance();
        continue;
      }

      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(
          diagnosticLoc(cursor.position()), ","_zc);
      return ast::NodeId();
    }

    if (hasTrailingComma && elems.size() == 1) {
      diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(tokenAt(end - 2).getLocation());
      return ast::NodeId();
    }

    ast::NodePayload payload;
    writeNodeList(payload, ast::kTupleTypeExprElemsFirstWord, ast::kTupleTypeExprElemsSizeWord,
                  builder.makeList(elems.asPtr()));
    return builder.makeNode(ast::SyntaxKind::TupleTypeExpr, rangeFor(start, end), payload);
  }

  ast::NodeId parseObjectTypeRange(ast::TreeBuilder& builder, size_t start, size_t end) const {
    zc::Vector<ast::NodeId> members;
    const size_t bodyEnd = end > start ? end - 1 : start;
    TokenCursor cursor = tokenCursorAt(start + 1);
    while (cursor.position() < bodyEnd) {
      if (cursor.peek() == ast::SyntaxKind::Comma || cursor.peek() == ast::SyntaxKind::Semicolon) {
        cursor.advance();
        continue;
      }

      const size_t memberStart = cursor.position();
      bool isMut = false;
      if (cursor.peek() == ast::SyntaxKind::MutKeyword) {
        isMut = true;
        cursor.advance();
      }

      const size_t nameIndex = cursor.position();
      if (nameIndex >= bodyEnd || cursor.peek() != ast::SyntaxKind::Identifier) {
        diagnosticEngine.diagnose<diagnostics::DiagID::IdentifierExpected>(
            diagnosticLoc(nameIndex));
        return ast::NodeId();
      }
      cursor.advance();

      bool isOptional = false;
      bool hasColon = false;
      if (cursor.position() < bodyEnd && cursor.peek() == ast::SyntaxKind::ErrorDefault) {
        isOptional = true;
        hasColon = true;
        cursor.advance();
      } else {
        if (cursor.position() < bodyEnd && cursor.peek() == ast::SyntaxKind::Question) {
          isOptional = true;
          cursor.advance();
        }
        if (cursor.position() < bodyEnd && cursor.peek() == ast::SyntaxKind::Colon) {
          hasColon = true;
          cursor.advance();
        }
      }
      if (!hasColon) {
        diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(
            diagnosticLoc(cursor.position()), ":"_zc);
        return ast::NodeId();
      }

      TypeParseResult ty = parseTypeExpression(builder, cursor, bodyEnd);
      if (!ty.node) {
        diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(
            diagnosticLoc(cursor.position()));
        return ast::NodeId();
      }

      ast::NodePayload memberPayload;
      writeIdent(memberPayload, ast::kObjectTypeMemberNameWord, internIdent(builder, nameIndex));
      writeNode(memberPayload, ast::kObjectTypeMemberTyWord, ty.node);
      memberPayload.words[ast::kObjectTypeMemberIsMutWord] = isMut ? 1 : 0;
      memberPayload.words[ast::kObjectTypeMemberIsOptionalWord] = isOptional ? 1 : 0;
      members.add(builder.makeNode(ast::SyntaxKind::ObjectTypeMember,
                                   rangeFor(memberStart, cursor.position()), memberPayload));

      if (cursor.position() >= bodyEnd) { break; }
      if (cursor.peek() == ast::SyntaxKind::Comma || cursor.peek() == ast::SyntaxKind::Semicolon) {
        cursor.advance();
        continue;
      }

      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(
          diagnosticLoc(cursor.position()), ","_zc);
      return ast::NodeId();
    }

    ast::NodePayload payload;
    writeNodeList(payload, ast::kObjectTypeExprMembersFirstWord,
                  ast::kObjectTypeExprMembersSizeWord, builder.makeList(members.asPtr()));
    return builder.makeNode(ast::SyntaxKind::ObjectTypeExpr, rangeFor(start, end), payload);
  }

  TypeParseResult parseTypeExpression(ast::TreeBuilder& builder, TokenCursor& cursor,
                                      size_t limit) const {
    return parseUnionType(builder, cursor, limit);
  }

  TypeParseResult parseTypeExpressionAt(ast::TreeBuilder& builder, size_t start,
                                        size_t limit) const {
    TokenCursor cursor = tokenCursorAt(start);
    return parseTypeExpression(builder, cursor, limit);
  }

  TypeParseResult parseUnionType(ast::TreeBuilder& builder, TokenCursor& cursor,
                                 size_t limit) const {
    const size_t start = cursor.position();
    zc::Vector<ast::NodeId> alts;
    TypeParseResult first = parseIntersectionType(builder, cursor, limit);
    if (!first.node) { return first; }
    alts.add(first.node);

    while (cursor.position() < limit && cursor.peek() == ast::SyntaxKind::Bar) {
      const size_t bar = cursor.position();
      cursor.advance();
      TypeParseResult next = parseIntersectionType(builder, cursor, limit);
      if (!next.node) {
        diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(bar + 1));
        return TypeParseResult();
      }
      alts.add(next.node);
    }

    if (alts.size() == 1) { return first; }

    ast::NodePayload payload;
    writeNodeList(payload, ast::kUnionTypeExprAltsFirstWord, ast::kUnionTypeExprAltsSizeWord,
                  builder.makeList(alts.asPtr()));
    return TypeParseResult{builder.makeNode(ast::SyntaxKind::UnionTypeExpr,
                                            rangeFor(start, cursor.position()), payload),
                           cursor.position()};
  }

  TypeParseResult parseIntersectionType(ast::TreeBuilder& builder, TokenCursor& cursor,
                                        size_t limit) const {
    const size_t start = cursor.position();
    zc::Vector<ast::NodeId> alts;
    TypeParseResult first = parsePostfixType(builder, cursor, limit);
    if (!first.node) { return first; }
    alts.add(first.node);

    while (cursor.position() < limit && cursor.peek() == ast::SyntaxKind::Ampersand) {
      const size_t ampersand = cursor.position();
      cursor.advance();
      TypeParseResult next = parsePostfixType(builder, cursor, limit);
      if (!next.node) {
        diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(ampersand + 1));
        return TypeParseResult();
      }
      alts.add(next.node);
    }

    if (alts.size() == 1) { return first; }

    ast::NodePayload payload;
    writeNodeList(payload, ast::kIntersectionTypeExprAltsFirstWord,
                  ast::kIntersectionTypeExprAltsSizeWord, builder.makeList(alts.asPtr()));
    return TypeParseResult{builder.makeNode(ast::SyntaxKind::IntersectionTypeExpr,
                                            rangeFor(start, cursor.position()), payload),
                           cursor.position()};
  }

  TypeParseResult parsePostfixType(ast::TreeBuilder& builder, TokenCursor& cursor,
                                   size_t limit) const {
    const size_t start = cursor.position();
    TypeParseResult result = parseAtomType(builder, cursor, limit);
    if (!result.node) { return result; }

    while (cursor.position() < limit) {
      if (cursor.peek() == ast::SyntaxKind::Question ||
          cursor.peek() == ast::SyntaxKind::QuestionQuestion) {
        const size_t suffix = cursor.position();
        ast::NodePayload payload;
        writeNode(payload, ast::kOptionalTypeExprInnerWord, result.node);
        payload.words[ast::kOptionalTypeExprDoubleWord] =
            cursor.peek() == ast::SyntaxKind::QuestionQuestion ? 1 : 0;
        result.node = builder.makeNode(ast::SyntaxKind::OptionalTypeExpr,
                                       rangeFor(start, suffix + 1), payload);
        cursor.advance();
        result.next = cursor.position();
        continue;
      }

      if (cursor.peek() == ast::SyntaxKind::LeftBracket) {
        const size_t openBracket = cursor.position();
        const size_t closeBracket = findMatchingRightBracket(openBracket, limit);
        if (closeBracket >= limit) {
          diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(openBracket),
                                                                        "]"_zc);
          return TypeParseResult();
        }

        ast::NodePayload payload;
        writeNode(payload, ast::kArrayTypeExprElemWord, result.node);
        if (openBracket + 1 < closeBracket) {
          const ast::NodeId lenExpr = parseExpressionRange(builder, openBracket + 1, closeBracket);
          if (!lenExpr) { return TypeParseResult(); }
          writeNode(payload, ast::kArrayTypeExprLenExprWord, lenExpr);
        }
        result.node = builder.makeNode(ast::SyntaxKind::ArrayTypeExpr,
                                       rangeFor(start, closeBracket + 1), payload);
        cursor.moveTo(closeBracket + 1);
        result.next = cursor.position();
        continue;
      }

      break;
    }

    return result;
  }

  TypeParseResult parseFunctionType(ast::TreeBuilder& builder, TokenCursor& cursor,
                                    size_t limit) const {
    const size_t start = cursor.position();
    const TokenCursor::Mark mark = cursor.mark();
    if (cursor.position() < limit && cursor.peek() == ast::SyntaxKind::FunKeyword) {
      cursor.advance();
    }

    if (cursor.position() < limit && cursor.peek() == ast::SyntaxKind::LessThan) {
      const size_t closeAngle = findMatchingAngleClose(cursor.position(), limit);
      if (closeAngle >= limit) {
        cursor.rewind(mark);
        return TypeParseResult();
      }
      cursor.moveTo(closeAngle + 1);
    }

    if (cursor.position() >= limit || cursor.peek() != ast::SyntaxKind::LeftParen) {
      cursor.rewind(mark);
      return TypeParseResult();
    }

    const size_t openParen = cursor.position();
    const size_t closeParen = findMatchingRightParen(openParen, limit);
    if (closeParen >= limit || closeParen + 1 >= limit ||
        kindAt(closeParen + 1) != ast::SyntaxKind::Arrow) {
      cursor.rewind(mark);
      return TypeParseResult();
    }

    const size_t retStart = closeParen + 2;
    cursor.moveTo(retStart);
    TypeParseResult ret = parseTypeExpression(builder, cursor, limit);
    if (!ret.node) {
      diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(retStart));
      return TypeParseResult();
    }

    ast::NodeId raisesTy;
    if (cursor.position() < limit && cursor.peek() == ast::SyntaxKind::RaisesKeyword) {
      const size_t raisesKeyword = cursor.position();
      cursor.advance();
      TypeParseResult raises = parseTypeExpression(builder, cursor, limit);
      if (!raises.node) {
        diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(
            diagnosticLoc(raisesKeyword + 1));
        return TypeParseResult();
      }
      raisesTy = raises.node;
    }

    ast::NodePayload payload;
    writeNodeList(payload, ast::kFunctionTypeExprParamsFirstWord,
                  ast::kFunctionTypeExprParamsSizeWord,
                  parseFunctionTypeParameters(builder, openParen + 1, closeParen));
    writeNode(payload, ast::kFunctionTypeExprRetTyWord, ret.node);
    if (raisesTy) { writeNode(payload, ast::kFunctionTypeExprRaisesWord, raisesTy); }
    return TypeParseResult{builder.makeNode(ast::SyntaxKind::FunctionTypeExpr,
                                            rangeFor(start, cursor.position()), payload),
                           cursor.position()};
  }

  TypeParseResult parseParenthesizedOrTupleType(ast::TreeBuilder& builder, TokenCursor& cursor,
                                                size_t limit) const {
    const size_t start = cursor.position();
    const size_t closeParen = findMatchingRightParen(start, limit);
    if (closeParen >= limit) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(start), ")"_zc);
      return TypeParseResult();
    }

    if (start + 1 == closeParen ||
        findTopLevelTypeToken(start + 1, closeParen, ast::SyntaxKind::Comma) < closeParen) {
      const ast::NodeId tuple = parseTupleTypeRange(builder, start, closeParen + 1);
      if (tuple) { cursor.moveTo(closeParen + 1); }
      return TypeParseResult{tuple, tuple ? cursor.position() : start};
    }

    cursor.moveTo(start + 1);
    TypeParseResult inner = parseTypeExpression(builder, cursor, closeParen);
    if (!inner.node) { return inner; }
    if (cursor.position() != closeParen) {
      diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
          diagnosticLoc(cursor.position()));
      return TypeParseResult();
    }
    cursor.moveTo(closeParen + 1);
    inner.next = cursor.position();
    return inner;
  }

  TypeParseResult parseBracketedType(ast::TreeBuilder& builder, TokenCursor& cursor,
                                     size_t limit) const {
    const size_t start = cursor.position();
    const size_t closeBracket = findMatchingRightBracket(start, limit);
    if (closeBracket >= limit) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(start), "]"_zc);
      return TypeParseResult();
    }

    const size_t semi = findTopLevelTypeToken(start + 1, closeBracket, ast::SyntaxKind::Semicolon);
    ast::NodePayload payload;
    if (semi < closeBracket) {
      const ast::NodeId elem = parseTypeRange(builder, start + 1, semi);
      const ast::NodeId lenExpr = parseExpressionRange(builder, semi + 1, closeBracket);
      if (!elem || !lenExpr) { return TypeParseResult(); }
      writeNode(payload, ast::kFixedArrayTypeExprElemWord, elem);
      writeNode(payload, ast::kFixedArrayTypeExprLenExprWord, lenExpr);
      cursor.moveTo(closeBracket + 1);
      return TypeParseResult{builder.makeNode(ast::SyntaxKind::FixedArrayTypeExpr,
                                              rangeFor(start, closeBracket + 1), payload),
                             cursor.position()};
    }

    const ast::NodeId elem = parseTypeRange(builder, start + 1, closeBracket);
    if (!elem) { return TypeParseResult(); }
    writeNode(payload, ast::kSliceArrayTypeExprElemWord, elem);
    cursor.moveTo(closeBracket + 1);
    return TypeParseResult{builder.makeNode(ast::SyntaxKind::SliceArrayTypeExpr,
                                            rangeFor(start, closeBracket + 1), payload),
                           cursor.position()};
  }

  TypeParseResult parseTypeQuery(ast::TreeBuilder& builder, TokenCursor& cursor,
                                 size_t limit) const {
    const size_t start = cursor.position();
    const size_t pathStart = start + 1;
    const size_t pathEnd = findTypePathEnd(pathStart, limit);
    if (pathEnd == pathStart) {
      diagnosticEngine.diagnose<diagnostics::DiagID::IdentifierExpected>(diagnosticLoc(pathStart));
      return TypeParseResult();
    }

    ast::NodePayload payload;
    writeNode(payload, ast::kTypeQueryExprPathWord, makeModulePath(builder, pathStart, pathEnd));
    cursor.moveTo(pathEnd);
    return TypeParseResult{
        builder.makeNode(ast::SyntaxKind::TypeQueryExpr, rangeFor(start, pathEnd), payload),
        cursor.position()};
  }

  ast::NodeList parseTypeArgumentListRange(ast::TreeBuilder& builder, size_t start,
                                           size_t closeAngle) const {
    zc::Vector<ast::NodeId> args;
    TokenCursor cursor = tokenCursorAt(start);
    while (cursor.position() < closeAngle) {
      if (cursor.peek() == ast::SyntaxKind::Comma) {
        cursor.advance();
        continue;
      }

      const size_t itemStart = cursor.position();
      TypeParseResult item = parseTypeExpression(builder, cursor, closeAngle + 1);
      if (!item.node) {
        diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(itemStart));
        break;
      }
      args.add(item.node);

      if (cursor.position() >= closeAngle) { break; }
      if (cursor.peek() == ast::SyntaxKind::Comma) {
        cursor.advance();
        continue;
      }

      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(
          diagnosticLoc(cursor.position()), ","_zc);
      break;
    }

    return builder.makeList(args.asPtr());
  }

  TypeParseResult parseNamedType(ast::TreeBuilder& builder, TokenCursor& cursor,
                                 size_t limit) const {
    const size_t start = cursor.position();
    const size_t pathEnd = findTypePathEnd(start, limit);
    if (pathEnd == start) { return TypeParseResult(); }

    cursor.moveTo(pathEnd);
    ast::NodeList args;
    if (cursor.position() < limit && cursor.peek() == ast::SyntaxKind::LessThan) {
      const size_t openAngle = cursor.position();
      const size_t closeAngle = findMatchingAngleClose(openAngle, limit);
      const bool invalidArgs = diagnoseTypeArgumentListSyntax(openAngle, closeAngle, limit);
      if (invalidArgs) { return TypeParseResult(); }
      if (closeAngle < limit) {
        args = parseTypeArgumentListRange(builder, openAngle + 1, closeAngle);
        cursor.moveTo(closeAngle + 1);
      }
    }

    ast::NodePayload payload;
    writeNode(payload, ast::kNamedTypeExprPathWord, makeModulePath(builder, start, pathEnd));
    writeNodeList(payload, ast::kNamedTypeExprArgsFirstWord, ast::kNamedTypeExprArgsSizeWord, args);
    return TypeParseResult{builder.makeNode(ast::SyntaxKind::NamedTypeExpr,
                                            rangeFor(start, cursor.position()), payload),
                           cursor.position()};
  }

  TypeParseResult parseAtomType(ast::TreeBuilder& builder, TokenCursor& cursor,
                                size_t limit) const {
    const size_t start = cursor.position();
    if (start >= limit) { return TypeParseResult(); }

    TypeParseResult functionType = parseFunctionType(builder, cursor, limit);
    if (functionType.node) { return functionType; }
    cursor.moveTo(start);

    switch (cursor.peek()) {
      case ast::SyntaxKind::LeftParen:
        return parseParenthesizedOrTupleType(builder, cursor, limit);
      case ast::SyntaxKind::TypeOfKeyword:
        return parseTypeQuery(builder, cursor, limit);
      case ast::SyntaxKind::LeftBrace: {
        const size_t closeBrace = findMatchingRightBrace(start, limit);
        if (closeBrace >= limit) {
          diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(start),
                                                                        "}"_zc);
          return TypeParseResult();
        }
        const ast::NodeId object = parseObjectTypeRange(builder, start, closeBrace + 1);
        if (object) { cursor.moveTo(closeBrace + 1); }
        return TypeParseResult{object, object ? cursor.position() : start};
      }
      case ast::SyntaxKind::LeftBracket:
        return parseBracketedType(builder, cursor, limit);
      default:
        break;
    }

    if (isPrimitiveTypeKeyword(cursor.peek())) {
      ast::NodePayload payload;
      payload.words[ast::kPredefinedTypeExprKindWord] = predefinedTypeCode(cursor.peek());
      cursor.advance();
      return TypeParseResult{builder.makeNode(ast::SyntaxKind::PredefinedTypeExpr,
                                              rangeFor(start, start + 1), payload),
                             cursor.position()};
    }

    return parseNamedType(builder, cursor, limit);
  }

  ast::NodeId parseTypeRange(ast::TreeBuilder& builder, size_t start, size_t end) const {
    while (start < end && kindAt(end - 1) == ast::SyntaxKind::Semicolon) { --end; }
    if (start >= end) { return ast::NodeId(); }

    TypeParseResult parsed = parseTypeExpressionAt(builder, start, end);
    if (!parsed.node) { return ast::NodeId(); }
    if (parsed.next != end) {
      diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
          diagnosticLoc(parsed.next));
      return ast::NodeId();
    }
    return parsed.node;
  }

  size_t findTopLevelBinaryOperator(size_t start, size_t end) const {
    int32_t parenDepth = 0;
    int32_t bracketDepth = 0;
    int32_t braceDepth = 0;
    int32_t bestPrecedence = 100;
    size_t best = end;

    for (size_t index = end; index > start;) {
      --index;
      const ast::SyntaxKind kind = kindAt(index);
      if (kind == ast::SyntaxKind::RightParen) {
        ++parenDepth;
      } else if (kind == ast::SyntaxKind::LeftParen) {
        if (parenDepth > 0) { --parenDepth; }
      } else if (kind == ast::SyntaxKind::RightBracket) {
        ++bracketDepth;
      } else if (kind == ast::SyntaxKind::LeftBracket) {
        if (bracketDepth > 0) { --bracketDepth; }
      } else if (kind == ast::SyntaxKind::RightBrace) {
        ++braceDepth;
      } else if (kind == ast::SyntaxKind::LeftBrace) {
        if (braceDepth > 0) { --braceDepth; }
      }

      if (parenDepth == 0 && bracketDepth == 0 && braceDepth == 0) {
        const int32_t precedence = binaryPrecedence(kind);
        if (precedence > 0 &&
            (index == start || !canEndExpressionBeforeBinary(kindAt(index - 1)))) {
          continue;
        }
        if (precedence > 0 &&
            (precedence < bestPrecedence ||
             (precedence == bestPrecedence && kind == ast::SyntaxKind::AsteriskAsterisk))) {
          bestPrecedence = precedence;
          best = index;
        }
      }
    }
    return best;
  }

  size_t findTopLevelAssignmentOperator(size_t start, size_t end) const {
    int32_t parenDepth = 0;
    int32_t bracketDepth = 0;
    int32_t braceDepth = 0;
    for (size_t index = start; index < end; ++index) {
      const ast::SyntaxKind kind = kindAt(index);
      if (parenDepth == 0 && bracketDepth == 0 && braceDepth == 0 && isAssignmentOperator(kind)) {
        return index;
      }

      if (kind == ast::SyntaxKind::LeftParen) {
        ++parenDepth;
      } else if (kind == ast::SyntaxKind::RightParen) {
        if (parenDepth > 0) { --parenDepth; }
      } else if (kind == ast::SyntaxKind::LeftBracket) {
        ++bracketDepth;
      } else if (kind == ast::SyntaxKind::RightBracket) {
        if (bracketDepth > 0) { --bracketDepth; }
      } else if (kind == ast::SyntaxKind::LeftBrace) {
        ++braceDepth;
      } else if (kind == ast::SyntaxKind::RightBrace) {
        if (braceDepth > 0) { --braceDepth; }
      }
    }
    return end;
  }

  size_t findTopLevelConditionalColon(size_t question, size_t end) const {
    int32_t parenDepth = 0;
    int32_t bracketDepth = 0;
    int32_t braceDepth = 0;
    int32_t nestedConditionalDepth = 0;
    for (size_t index = question + 1; index < end; ++index) {
      const ast::SyntaxKind kind = kindAt(index);
      if (parenDepth == 0 && bracketDepth == 0 && braceDepth == 0) {
        if (kind == ast::SyntaxKind::Question) {
          ++nestedConditionalDepth;
          continue;
        }
        if (kind == ast::SyntaxKind::Colon) {
          if (nestedConditionalDepth == 0) { return index; }
          --nestedConditionalDepth;
          continue;
        }
      }

      if (kind == ast::SyntaxKind::LeftParen) {
        ++parenDepth;
      } else if (kind == ast::SyntaxKind::RightParen) {
        if (parenDepth > 0) { --parenDepth; }
      } else if (kind == ast::SyntaxKind::LeftBracket) {
        ++bracketDepth;
      } else if (kind == ast::SyntaxKind::RightBracket) {
        if (bracketDepth > 0) { --bracketDepth; }
      } else if (kind == ast::SyntaxKind::LeftBrace) {
        ++braceDepth;
      } else if (kind == ast::SyntaxKind::RightBrace) {
        if (braceDepth > 0) { --braceDepth; }
      }
    }
    return end;
  }

  size_t consumeCommaDelimitedItem(TokenCursor& cursor, size_t end) const {
    int32_t parenDepth = 0;
    int32_t bracketDepth = 0;
    int32_t braceDepth = 0;

    while (cursor.position() < end) {
      const ast::SyntaxKind kind = cursor.peek();
      if (parenDepth == 0 && bracketDepth == 0 && braceDepth == 0 &&
          kind == ast::SyntaxKind::Comma) {
        return cursor.position();
      }

      if (kind == ast::SyntaxKind::LessThan) {
        const size_t closeAngle = findMatchingAngleClose(cursor.position(), end);
        if (closeAngle + 1 < end && kindAt(closeAngle + 1) == ast::SyntaxKind::LeftParen) {
          cursor.moveTo(closeAngle + 1);
          continue;
        }
      }

      if (kind == ast::SyntaxKind::LeftParen) {
        ++parenDepth;
      } else if (kind == ast::SyntaxKind::RightParen) {
        if (parenDepth > 0) { --parenDepth; }
      } else if (kind == ast::SyntaxKind::LeftBracket) {
        ++bracketDepth;
      } else if (kind == ast::SyntaxKind::RightBracket) {
        if (bracketDepth > 0) { --bracketDepth; }
      } else if (kind == ast::SyntaxKind::LeftBrace) {
        ++braceDepth;
      } else if (kind == ast::SyntaxKind::RightBrace) {
        if (braceDepth > 0) { --braceDepth; }
      }
      cursor.advance();
    }

    return cursor.position();
  }

  ast::NodeId parseExpressionList(ast::TreeBuilder& builder, size_t start, size_t end,
                                  ast::SyntaxKind containerKind) const {
    zc::Vector<ast::NodeId> expressions;
    TokenCursor cursor = tokenCursorAt(start);
    while (cursor.position() < end) {
      const size_t itemStart = cursor.position();
      const size_t itemEnd = consumeCommaDelimitedItem(cursor, end);
      if (itemStart < itemEnd) {
        const size_t rest = findTopLevelToken(itemStart, itemEnd, ast::SyntaxKind::DotDotDot);
        if (rest < itemEnd) {
          diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
              tokenAt(rest).getLocation());
        } else {
          addNodeIfPresent(expressions, parseExpressionRange(builder, itemStart, itemEnd));
        }
      }
      if (cursor.position() < end && cursor.peek() == ast::SyntaxKind::Comma) { cursor.advance(); }
    }
    const ast::NodeList list = builder.makeList(expressions.asPtr());
    ast::NodePayload payload;
    if (containerKind == ast::SyntaxKind::ArrayLiteral) {
      writeNodeList(payload, ast::kArrayLiteralElemsFirstWord, ast::kArrayLiteralElemsSizeWord,
                    list);
    } else {
      writeNodeList(payload, ast::kTupleLiteralElemsFirstWord, ast::kTupleLiteralElemsSizeWord,
                    list);
    }
    return builder.makeNode(containerKind, rangeFor(start, end), payload);
  }

  ast::NodeId parseCommaExpression(ast::TreeBuilder& builder, size_t start, size_t end) const {
    zc::Vector<ast::NodeId> expressions;
    TokenCursor cursor = tokenCursorAt(start);
    while (cursor.position() < end) {
      const size_t itemStart = cursor.position();
      const size_t itemEnd = consumeCommaDelimitedItem(cursor, end);
      if (itemStart >= itemEnd) { return ast::NodeId(); }
      const ast::NodeId expr = parseExpressionRange(builder, itemStart, itemEnd);
      if (!expr) { return ast::NodeId(); }
      expressions.add(expr);
      if (cursor.position() < end && cursor.peek() == ast::SyntaxKind::Comma) { cursor.advance(); }
    }
    ast::NodePayload payload;
    writeNodeList(payload, ast::kCommaExprElemsFirstWord, ast::kCommaExprElemsSizeWord,
                  builder.makeList(expressions.asPtr()));
    return builder.makeNode(ast::SyntaxKind::CommaExpr, rangeFor(start, end), payload);
  }

  ast::NodeList parseExpressionArguments(ast::TreeBuilder& builder, size_t start,
                                         size_t end) const {
    zc::Vector<ast::NodeId> args;
    TokenCursor cursor = tokenCursorAt(start);
    while (cursor.position() < end) {
      const size_t itemStart = cursor.position();
      const size_t itemEnd = consumeCommaDelimitedItem(cursor, end);
      if (itemStart < itemEnd) {
        addNodeIfPresent(args, parseExpressionRange(builder, itemStart, itemEnd));
      }
      if (cursor.position() < end && cursor.peek() == ast::SyntaxKind::Comma) { cursor.advance(); }
    }
    return builder.makeList(args.asPtr());
  }

  ast::NodeList parseTypeArguments(ast::TreeBuilder& builder, size_t start, size_t end) const {
    return parseTypeArgumentListRange(builder, start, end);
  }

  size_t findTrailingCallOpen(size_t start, size_t end) const {
    if (end <= start || kindAt(end - 1) != ast::SyntaxKind::RightParen) { return end; }
    for (size_t index = end - 1; index > start;) {
      --index;
      if (kindAt(index) == ast::SyntaxKind::LeftParen &&
          rangeIsWrapped(index, end, ast::SyntaxKind::LeftParen, ast::SyntaxKind::RightParen)) {
        return index;
      }
    }
    return end;
  }

  size_t findTrailingIndexOpen(size_t start, size_t end) const {
    if (end <= start || kindAt(end - 1) != ast::SyntaxKind::RightBracket) { return end; }
    for (size_t index = end - 1; index > start;) {
      --index;
      if (kindAt(index) == ast::SyntaxKind::LeftBracket &&
          rangeIsWrapped(index, end, ast::SyntaxKind::LeftBracket, ast::SyntaxKind::RightBracket)) {
        return index;
      }
    }
    return end;
  }

  size_t findTrailingTypeArgumentOpen(size_t start, size_t end) const {
    if (end <= start || kindAt(end - 1) != ast::SyntaxKind::GreaterThan) { return end; }

    int32_t depth = 0;
    for (size_t index = end; index > start;) {
      --index;
      const ast::SyntaxKind kind = kindAt(index);
      if (kind == ast::SyntaxKind::GreaterThan) {
        ++depth;
      } else if (kind == ast::SyntaxKind::LessThan) {
        --depth;
        if (depth == 0) { return index; }
      }
    }
    return end;
  }

  size_t findTrailingMemberOperator(size_t start, size_t end) const {
    int32_t parenDepth = 0;
    int32_t bracketDepth = 0;
    int32_t braceDepth = 0;
    int32_t angleDepth = 0;

    for (size_t index = end; index > start;) {
      --index;
      const ast::SyntaxKind kind = kindAt(index);
      if (kind == ast::SyntaxKind::RightParen) {
        ++parenDepth;
      } else if (kind == ast::SyntaxKind::LeftParen) {
        if (parenDepth > 0) { --parenDepth; }
      } else if (kind == ast::SyntaxKind::RightBracket) {
        ++bracketDepth;
      } else if (kind == ast::SyntaxKind::LeftBracket) {
        if (bracketDepth > 0) { --bracketDepth; }
      } else if (kind == ast::SyntaxKind::RightBrace) {
        ++braceDepth;
      } else if (kind == ast::SyntaxKind::LeftBrace) {
        if (braceDepth > 0) { --braceDepth; }
      } else if (kind == ast::SyntaxKind::GreaterThan) {
        ++angleDepth;
      } else if (kind == ast::SyntaxKind::LessThan) {
        if (angleDepth > 0) { --angleDepth; }
      }

      if (parenDepth == 0 && bracketDepth == 0 && braceDepth == 0 && angleDepth == 0 &&
          (kind == ast::SyntaxKind::Period || kind == ast::SyntaxKind::QuestionDot) &&
          index + 1 < end && isPropertyNameLike(kindAt(index + 1))) {
        return index;
      }
    }
    return end;
  }

  bool canUseRangeAsCallCallee(size_t start, size_t end) const {
    if (start >= end) { return false; }
    if (findTopLevelToken(start, end, ast::SyntaxKind::Comma) < end) { return false; }
    if (findTopLevelAssignmentOperator(start, end) < end) { return false; }
    if (findTopLevelToken(start, end, ast::SyntaxKind::Question) < end) { return false; }
    if (findTopLevelToken(start, end, ast::SyntaxKind::QuestionQuestion) < end) { return false; }
    if (findTopLevelToken(start, end, ast::SyntaxKind::ErrorDefault) < end) { return false; }
    return findTopLevelBinaryOperator(start, end) == end;
  }

  ast::NodeId parseCallExpression(ast::TreeBuilder& builder, size_t start, size_t openParen,
                                  size_t end) const {
    const size_t calleeEnd =
        openParen > start && kindAt(openParen - 1) == ast::SyntaxKind::QuestionDot ? openParen - 1
                                                                                   : openParen;
    const size_t typeArgsOpen = findTrailingTypeArgumentOpen(start, calleeEnd);
    const size_t parsedCalleeEnd = typeArgsOpen < calleeEnd ? typeArgsOpen : calleeEnd;
    const ast::NodeId callee = parseExpressionRange(builder, start, parsedCalleeEnd);
    if (!callee) { return ast::NodeId(); }

    ast::NodePayload payload;
    writeNode(payload, ast::kCallExpressionCalleeWord, callee);
    if (typeArgsOpen < calleeEnd && typeArgsOpen + 1 < calleeEnd - 1) {
      writeNodeList(payload, ast::kCallExpressionTypeArgsFirstWord,
                    ast::kCallExpressionTypeArgsSizeWord,
                    parseTypeArguments(builder, typeArgsOpen + 1, calleeEnd - 1));
    }
    writeNodeList(payload, ast::kCallExpressionArgsFirstWord, ast::kCallExpressionArgsSizeWord,
                  parseExpressionArguments(builder, openParen + 1, end - 1));
    return builder.makeNode(ast::SyntaxKind::CallExpression, rangeFor(start, end), payload);
  }

  ast::NodeId parseNewExpression(ast::TreeBuilder& builder, size_t start, size_t end) const {
    const size_t openParen = findTrailingCallOpen(start + 1, end);
    const size_t calleeEnd = openParen < end ? openParen : end;
    const size_t typeArgsOpen = findTrailingTypeArgumentOpen(start + 1, calleeEnd);
    const size_t parsedCalleeEnd = typeArgsOpen < calleeEnd ? typeArgsOpen : calleeEnd;
    const ast::NodeId callee = parseExpressionRange(builder, start + 1, parsedCalleeEnd);
    if (!callee) { return ast::NodeId(); }

    ast::NodePayload payload;
    writeNode(payload, ast::kNewExpressionCalleeWord, callee);
    if (typeArgsOpen < calleeEnd && typeArgsOpen + 1 < calleeEnd - 1) {
      writeNodeList(payload, ast::kNewExpressionTypeArgsFirstWord,
                    ast::kNewExpressionTypeArgsSizeWord,
                    parseTypeArguments(builder, typeArgsOpen + 1, calleeEnd - 1));
    }
    if (openParen < end) {
      writeNodeList(payload, ast::kNewExpressionArgsFirstWord, ast::kNewExpressionArgsSizeWord,
                    parseExpressionArguments(builder, openParen + 1, end - 1));
    } else {
      zc::Vector<ast::NodeId> args;
      writeNodeList(payload, ast::kNewExpressionArgsFirstWord, ast::kNewExpressionArgsSizeWord,
                    builder.makeList(args.asPtr()));
    }
    return builder.makeNode(ast::SyntaxKind::NewExpression, rangeFor(start, end), payload);
  }

  ast::NodeId makeEmptyMacroPattern(ast::TreeBuilder& builder, size_t start, size_t end) const {
    zc::Vector<ast::NodeId> frags;
    ast::NodePayload payload;
    writeNodeList(payload, ast::kMacroPatternFragsFirstWord, ast::kMacroPatternFragsSizeWord,
                  builder.makeList(frags.asPtr()));
    return builder.makeNode(ast::SyntaxKind::MacroPattern, rangeFor(start, end), payload);
  }

  ast::NodeId makeEmptyMacroTokenTree(ast::TreeBuilder& builder, size_t start, size_t end) const {
    zc::Vector<ast::NodeId> tokens;
    ast::NodePayload payload;
    writeNodeList(payload, ast::kMacroTokenTreeTokensFirstWord, ast::kMacroTokenTreeTokensSizeWord,
                  builder.makeList(tokens.asPtr()));
    return builder.makeNode(ast::SyntaxKind::MacroTokenTree, rangeFor(start, end), payload);
  }

  ast::NodeId parseMacroInvocationExpression(ast::TreeBuilder& builder, size_t start,
                                             size_t end) const {
    if (!isMacroInvocationStart(start, end)) {
      diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(diagnosticLoc(start));
      return ast::NodeId();
    }

    const size_t groupOpen = start + 2;
    const size_t groupClose = findMatchingMacroGroup(groupOpen, end);
    if (groupClose >= end) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(
          diagnosticLoc(groupOpen), macroGroupCloseLabel(kindAt(groupOpen)));
      return ast::NodeId();
    }

    ast::NodePayload namePayload;
    writeIdent(namePayload, ast::kIdentExprNameWord, internIdent(builder, start));
    const ast::NodeId name =
        builder.makeNode(ast::SyntaxKind::IdentExpr, rangeFor(start, start + 1), namePayload);

    zc::Vector<ast::NodeId> tt;
    ast::NodePayload payload;
    writeNode(payload, ast::kMacroInvocationExprNameWord, name);
    payload.words[ast::kMacroInvocationExprBraceWord] = macroBraceCode(kindAt(groupOpen));
    writeNodeList(payload, ast::kMacroInvocationExprTtFirstWord,
                  ast::kMacroInvocationExprTtSizeWord, builder.makeList(tt.asPtr()));
    return builder.makeNode(ast::SyntaxKind::MacroInvocationExpr, rangeFor(start, groupClose + 1),
                            payload);
  }

  ast::NodeId parseUnsafeBlockExpression(ast::TreeBuilder& builder, size_t start,
                                         size_t end) const {
    if (start + 1 >= end || kindAt(start + 1) != ast::SyntaxKind::LeftBrace) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(start + 1),
                                                                    "{"_zc);
      return ast::NodeId();
    }

    ast::NodePayload payload;
    writeNode(payload, ast::kUnsafeBlockExprBodyWord, parseBlock(builder, start + 1, end, true));
    return builder.makeNode(ast::SyntaxKind::UnsafeBlockExpr, rangeFor(start, end), payload);
  }

  ast::NodeId parseSpawnExpression(ast::TreeBuilder& builder, size_t start, size_t end) const {
    uint8_t modFlags = 0;
    uint8_t priority = 0;
    size_t cursor = start + 1;

    while (cursor < end && kindAt(cursor) == ast::SyntaxKind::Identifier) {
      const zc::StringPtr modifier = tokenAt(cursor).getValue();
      if (modifier == "detached"_zc) {
        modFlags |= 1;
        ++cursor;
        continue;
      }
      if (modifier == "blocking"_zc) {
        modFlags |= 2;
        ++cursor;
        continue;
      }
      if (modifier == "priority"_zc) {
        if (cursor + 1 >= end || kindAt(cursor + 1) != ast::SyntaxKind::LeftParen) {
          diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(cursor + 1),
                                                                        "("_zc);
          return ast::NodeId();
        }
        const size_t closeParen = findMatchingRightParen(cursor + 1, end);
        if (closeParen >= end) {
          diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(end), ")"_zc);
          return ast::NodeId();
        }
        if (closeParen != cursor + 3 || kindAt(cursor + 2) != ast::SyntaxKind::Identifier) {
          diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
              diagnosticLoc(cursor + 2));
          return ast::NodeId();
        }
        const zc::StringPtr value = tokenAt(cursor + 2).getValue();
        if (value == "high"_zc) {
          priority = 1;
        } else if (value == "low"_zc) {
          priority = 2;
        } else {
          diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
              tokenAt(cursor + 2).getLocation());
          return ast::NodeId();
        }
        cursor = closeParen + 1;
        continue;
      }
      break;
    }

    if (cursor >= end) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpressionExpected>(diagnosticLoc(cursor));
      return ast::NodeId();
    }

    ast::NodeId body;
    if (kindAt(cursor) == ast::SyntaxKind::LeftBrace) {
      body = parseBlock(builder, cursor, end, true);
    } else {
      const ast::NodeId expr = parseRequiredExpression(builder, cursor, end);
      if (!expr) { return ast::NodeId(); }

      ast::NodePayload statementPayload;
      writeNode(statementPayload, ast::kExpressionStatementExpressionWord, expr);
      body = builder.makeNode(ast::SyntaxKind::ExpressionStatement, rangeFor(cursor, end),
                              statementPayload);
    }
    if (!body) { return ast::NodeId(); }

    ast::NodePayload payload;
    payload.words[ast::kSpawnExpressionModFlagsWord] = modFlags;
    payload.words[ast::kSpawnExpressionPriorityWord] = priority;
    writeNode(payload, ast::kSpawnExpressionBodyWord, body);
    return builder.makeNode(ast::SyntaxKind::SpawnExpression, rangeFor(start, end), payload);
  }

  ast::NodeId parseCastExpression(ast::TreeBuilder& builder, size_t start, size_t asIndex,
                                  size_t end) const {
    size_t typeStart = asIndex + 1;
    uint8_t mode = castModeCode(ast::SyntaxKind::AsKeyword);
    if (typeStart < end && kindAt(typeStart) == ast::SyntaxKind::Exclamation) {
      diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
          tokenAt(typeStart).getLocation());
      return ast::NodeId();
    }
    if (typeStart < end && kindAt(typeStart) == ast::SyntaxKind::Question) {
      mode = castModeCode(kindAt(typeStart));
      ++typeStart;
    }

    const ast::NodeId expr = parseExpressionRange(builder, start, asIndex);
    if (isStandaloneDynTypeRange(typeStart, end)) {
      diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
          tokenAt(typeStart).getLocation());
      return ast::NodeId();
    }
    const ast::NodeId ty = parseTypeRange(builder, typeStart, end);
    if (!expr || !ty) { return ast::NodeId(); }

    ast::NodePayload payload;
    payload.words[ast::kCastExpressionModeWord] = mode;
    writeNode(payload, ast::kCastExpressionExprWord, expr);
    writeNode(payload, ast::kCastExpressionTyWord, ty);
    return builder.makeNode(ast::SyntaxKind::CastExpression, rangeFor(start, end), payload);
  }

  ast::NodeId parseImportCallExpression(ast::TreeBuilder& builder, size_t start, size_t end) const {
    const size_t openParen = findTrailingCallOpen(start, end);
    ast::NodePayload payload;
    if (openParen < end) {
      writeNodeList(payload, ast::kImportCallExpressionArgsFirstWord,
                    ast::kImportCallExpressionArgsSizeWord,
                    parseExpressionArguments(builder, openParen + 1, end - 1));
    }
    return builder.makeNode(ast::SyntaxKind::ImportCallExpression, rangeFor(start, end), payload);
  }

  ast::NodeId parseCaptureItem(ast::TreeBuilder& builder, size_t start, size_t end) const {
    if (start >= end) { return ast::NodeId(); }

    size_t nameIndex = start;
    ast::CaptureMode mode = ast::CaptureMode::ByValue;
    if (kindAt(nameIndex) == ast::SyntaxKind::Ampersand) {
      mode = ast::CaptureMode::ByRef;
      ++nameIndex;
    }

    if (nameIndex >= end) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(end),
                                                                    "identifier"_zc);
      return ast::NodeId();
    }

    if (kindAt(nameIndex) == ast::SyntaxKind::ThisKeyword && mode == ast::CaptureMode::ByValue) {
      mode = ast::CaptureMode::This;
    } else if (kindAt(nameIndex) != ast::SyntaxKind::Identifier) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(nameIndex),
                                                                    "identifier"_zc);
      return ast::NodeId();
    }

    ast::NodePayload payload;
    payload.words[ast::kCaptureItemModeWord] = static_cast<uint32_t>(mode);
    writeIdent(payload, ast::kCaptureItemNameWord, internIdent(builder, nameIndex));
    return builder.makeNode(ast::SyntaxKind::CaptureItem, rangeFor(start, end), payload);
  }

  ast::NodeList parseCaptureList(ast::TreeBuilder& builder, size_t start, size_t end) const {
    zc::Vector<ast::NodeId> captures;
    TokenCursor cursor = tokenCursorAt(start);
    while (cursor.position() < end) {
      while (cursor.position() < end && cursor.peek() == ast::SyntaxKind::Comma) {
        cursor.advance();
      }
      if (cursor.position() >= end) { break; }

      const size_t itemStart = cursor.position();
      const size_t itemEnd = consumeCommaDelimitedItem(cursor, end);
      addNodeIfPresent(captures, parseCaptureItem(builder, itemStart, itemEnd));
      if (cursor.position() < end && cursor.peek() == ast::SyntaxKind::Comma) { cursor.advance(); }
    }
    return builder.makeList(captures.asPtr());
  }

  ast::NodeId parseFunctionExpression(ast::TreeBuilder& builder, size_t start, size_t end) const {
    size_t openParen = end;
    for (size_t index = start + 1; index < end; ++index) {
      if (kindAt(index) == ast::SyntaxKind::LeftParen) {
        openParen = index;
        break;
      }
    }
    const size_t closeParen = openParen < end ? findMatchingRightParen(openParen, end) : end;
    const size_t bodyOpen = findTopLevelToken(closeParen + 1, end, ast::SyntaxKind::LeftBrace);
    if (openParen >= end || closeParen >= end || bodyOpen >= end) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(end), "{"_zc);
      return ast::NodeId();
    }

    const size_t signatureEnd = bodyOpen < end ? bodyOpen : end;
    const size_t arrow = findTopLevelToken(closeParen + 1, signatureEnd, ast::SyntaxKind::Arrow);
    const size_t raises = arrow < signatureEnd ? findTopLevelToken(arrow + 1, signatureEnd,
                                                                   ast::SyntaxKind::RaisesKeyword)
                                               : end;

    ast::NodePayload payload;
    writeNode(payload, ast::kFunctionExpressionParamsIdWord,
              parseFunctionParameterList(builder, openParen, closeParen));

    size_t useIndex = end;
    for (size_t index = closeParen + 1; index < (bodyOpen < end ? bodyOpen : end); ++index) {
      zc::StringPtr text = tokenAt(index).getValue();
      if (text.size() == 0) { text = tokenLabel(tokenAt(index)); }
      if (kindAt(index) == ast::SyntaxKind::Identifier && text == "use"_zc) {
        useIndex = index;
        break;
      }
    }
    if (useIndex < end) {
      const size_t captureOpen = findTopLevelToken(useIndex + 1, bodyOpen < end ? bodyOpen : end,
                                                   ast::SyntaxKind::LeftBracket);
      if (captureOpen < end) {
        const size_t captureClose =
            findMatchingRightBracket(captureOpen, bodyOpen < end ? bodyOpen : end);
        const size_t captureEnd = captureClose < end ? captureClose : bodyOpen;
        writeNodeList(payload, ast::kFunctionExpressionCapturesFirstWord,
                      ast::kFunctionExpressionCapturesSizeWord,
                      parseCaptureList(builder, captureOpen + 1, captureEnd));
      }
    }

    if (arrow < signatureEnd) {
      const size_t retEnd = raises < signatureEnd ? raises : signatureEnd;
      const ast::NodeId retTy = parseTypeRange(builder, arrow + 1, retEnd);
      if (!retTy) {
        diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(arrow + 1));
        return ast::NodeId();
      }
      writeNode(payload, ast::kFunctionExpressionRetTyWord, retTy);
    }
    if (raises < signatureEnd) {
      const ast::NodeId raisesTy = parseTypeRange(builder, raises + 1, signatureEnd);
      if (!raisesTy) {
        diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(raises + 1));
        return ast::NodeId();
      }
      writeNode(payload, ast::kFunctionExpressionRaisesTyWord, raisesTy);
    }

    writeNode(payload, ast::kFunctionExpressionBodyWord, parseBlock(builder, bodyOpen, end));
    return builder.makeNode(ast::SyntaxKind::FunctionExpression, rangeFor(start, end), payload);
  }

  ast::NodeId parseLambdaExpression(ast::TreeBuilder& builder, size_t start, size_t end) const {
    if (kindAt(start) != ast::SyntaxKind::LeftParen) { return ast::NodeId(); }

    const size_t fatArrow = findTopLevelToken(start, end, ast::SyntaxKind::EqualsGreaterThan);
    if (fatArrow >= end) { return ast::NodeId(); }

    const size_t closeParen = findMatchingRightParen(start, end);
    if (closeParen >= fatArrow) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(start), ")"_zc);
      return ast::NodeId();
    }

    ast::NodePayload payload;
    writeNode(payload, ast::kLambdaExpressionParamsIdWord,
              parseFunctionParameterList(builder, start, closeParen));

    const size_t arrow = findTopLevelToken(closeParen + 1, fatArrow, ast::SyntaxKind::Arrow);
    if (arrow < fatArrow) {
      const size_t raises = findTopLevelToken(arrow + 1, fatArrow, ast::SyntaxKind::RaisesKeyword);
      const size_t retEnd = raises < fatArrow ? raises : fatArrow;
      const ast::NodeId retTy = parseTypeRange(builder, arrow + 1, retEnd);
      if (!retTy) {
        diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(arrow + 1));
        return ast::NodeId();
      }
      writeNode(payload, ast::kLambdaExpressionRetTyWord, retTy);

      if (raises < fatArrow) {
        const ast::NodeId raisesTy = parseTypeRange(builder, raises + 1, fatArrow);
        if (!raisesTy) {
          diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(raises + 1));
          return ast::NodeId();
        }
        writeNode(payload, ast::kLambdaExpressionRaisesTyWord, raisesTy);
      }
    }

    const size_t bodyStart = fatArrow + 1;
    if (bodyStart >= end) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(bodyStart),
                                                                    "lambda body"_zc);
      return ast::NodeId();
    }

    if (rangeIsWrapped(bodyStart, end, ast::SyntaxKind::LeftBrace, ast::SyntaxKind::RightBrace)) {
      writeNode(payload, ast::kLambdaExpressionBodyWord, parseBlock(builder, bodyStart, end));
    } else {
      const ast::NodeId exprBody = parseExpressionRange(builder, bodyStart, end);
      if (!exprBody) { return ast::NodeId(); }
      writeNode(payload, ast::kLambdaExpressionExprBodyWord, exprBody);
    }

    return builder.makeNode(ast::SyntaxKind::LambdaExpression, rangeFor(start, end), payload);
  }

  ast::NodeList parseObjectLiteralProperties(ast::TreeBuilder& builder, size_t start,
                                             size_t end) const {
    zc::Vector<ast::NodeId> properties;
    TokenCursor cursor = tokenCursorAt(start);
    while (cursor.position() < end) {
      const size_t itemStart = cursor.position();
      const size_t itemEnd = consumeCommaDelimitedItem(cursor, end);
      if (itemStart < itemEnd) {
        ast::NodePayload propertyPayload;
        if (kindAt(itemStart) == ast::SyntaxKind::DotDotDot) {
          writeNode(propertyPayload, ast::kObjectSpreadExprWord,
                    parseExpressionRange(builder, itemStart + 1, itemEnd));
          properties.add(builder.makeNode(ast::SyntaxKind::ObjectSpread,
                                          rangeFor(itemStart, itemEnd), propertyPayload));
        } else {
          const size_t colon = findTopLevelToken(itemStart, itemEnd, ast::SyntaxKind::Colon);
          writeIdent(propertyPayload, ast::kObjectPropertyNameWord,
                     internIdent(builder, itemStart));
          if (colon < itemEnd) {
            writeNode(propertyPayload, ast::kObjectPropertyValueWord,
                      parseExpressionRange(builder, colon + 1, itemEnd));
          } else {
            propertyPayload.words[ast::kObjectPropertyShortFormWord] = 1;
          }
          properties.add(builder.makeNode(ast::SyntaxKind::ObjectProperty,
                                          rangeFor(itemStart, itemEnd), propertyPayload));
        }
      }
      if (cursor.position() < end && cursor.peek() == ast::SyntaxKind::Comma) { cursor.advance(); }
    }

    return builder.makeList(properties.asPtr());
  }

  ast::NodeId parseObjectLiteralExpression(ast::TreeBuilder& builder, size_t start,
                                           size_t end) const {
    ast::NodePayload payload;
    writeNodeList(payload, ast::kObjectLiteralExprPropertiesFirstWord,
                  ast::kObjectLiteralExprPropertiesSizeWord,
                  parseObjectLiteralProperties(builder, start + 1, end - 1));
    return builder.makeNode(ast::SyntaxKind::ObjectLiteralExpr, rangeFor(start, end), payload);
  }

  ast::NodeId parseStructLiteralExpression(ast::TreeBuilder& builder, size_t start, size_t brace,
                                           size_t end) const {
    const ast::NodeId ty = parseTypeRange(builder, start, brace);
    if (!ty) { return ast::NodeId(); }

    ast::NodePayload payload;
    writeNode(payload, ast::kStructLiteralExprTyWord, ty);
    writeNodeList(payload, ast::kStructLiteralExprPropertiesFirstWord,
                  ast::kStructLiteralExprPropertiesSizeWord,
                  parseObjectLiteralProperties(builder, brace + 1, end - 1));
    return builder.makeNode(ast::SyntaxKind::StructLiteralExpr, rangeFor(start, end), payload);
  }

  ast::NodeId parseTemplateLiteralExpression(ast::TreeBuilder& builder, size_t start,
                                             size_t end) const {
    zc::Vector<ast::NodeId> exprs;
    size_t cursor = start + 1;
    while (cursor < end) {
      const ast::SyntaxKind kind = kindAt(cursor);
      if (kind == ast::SyntaxKind::TemplateMiddle || kind == ast::SyntaxKind::TemplateTail) {
        ++cursor;
        continue;
      }
      const size_t segmentEnd = findTopLevelToken(cursor, end, ast::SyntaxKind::TemplateMiddle);
      const size_t tail = findTopLevelToken(cursor, end, ast::SyntaxKind::TemplateTail);
      const size_t exprEnd = segmentEnd < tail ? segmentEnd : tail;
      if (cursor < exprEnd) {
        addNodeIfPresent(exprs, parseExpressionRange(builder, cursor, exprEnd));
      }
      cursor = exprEnd < end ? exprEnd + 1 : end;
    }

    ast::NodePayload payload;
    writeNodeList(payload, ast::kTemplateLiteralExprExprsFirstWord,
                  ast::kTemplateLiteralExprExprsSizeWord, builder.makeList(exprs.asPtr()));
    return builder.makeNode(ast::SyntaxKind::TemplateLiteralExpr, rangeFor(start, end), payload);
  }

  struct ExpressionParseResult {
    ast::NodeId node;
    size_t next = 0;
  };

  ExpressionParseResult parseExpressionAt(ast::TreeBuilder& builder, size_t start,
                                          size_t limit) const {
    return parseCommaExpressionAt(builder, start, limit);
  }

  ExpressionParseResult parseCommaExpressionAt(ast::TreeBuilder& builder, size_t start,
                                               size_t limit) const {
    zc::Vector<ast::NodeId> expressions;
    ExpressionParseResult first = parseAssignmentExpressionAt(builder, start, limit);
    if (!first.node) { return first; }
    expressions.add(first.node);

    size_t cursor = first.next;
    while (cursor < limit && kindAt(cursor) == ast::SyntaxKind::Comma) {
      ExpressionParseResult next = parseAssignmentExpressionAt(builder, cursor + 1, limit);
      if (!next.node) {
        diagnoseExpressionExpected(cursor + 1);
        return ExpressionParseResult();
      }
      expressions.add(next.node);
      cursor = next.next;
    }

    if (expressions.size() == 1) { return first; }

    ast::NodePayload payload;
    writeNodeList(payload, ast::kCommaExprElemsFirstWord, ast::kCommaExprElemsSizeWord,
                  builder.makeList(expressions.asPtr()));
    return {builder.makeNode(ast::SyntaxKind::CommaExpr, rangeFor(start, cursor), payload), cursor};
  }

  ExpressionParseResult parseAssignmentExpressionAt(ast::TreeBuilder& builder, size_t start,
                                                    size_t limit) const {
    ExpressionParseResult lhs = parseConditionalExpressionAt(builder, start, limit);
    if (!lhs.node) { return lhs; }
    if (lhs.next >= limit || !isAssignmentOperator(kindAt(lhs.next))) { return lhs; }

    const size_t opIndex = lhs.next;
    ExpressionParseResult rhs = parseAssignmentExpressionAt(builder, opIndex + 1, limit);
    if (!rhs.node) {
      diagnoseExpressionExpected(opIndex + 1);
      return ExpressionParseResult();
    }

    ast::NodePayload payload;
    payload.words[ast::kAssignmentExprOpWord] = assignmentOpCode(kindAt(opIndex));
    writeNode(payload, ast::kAssignmentExprLhsWord, lhs.node);
    writeNode(payload, ast::kAssignmentExprRhsWord, rhs.node);
    return {builder.makeNode(ast::SyntaxKind::AssignmentExpr, rangeFor(start, rhs.next), payload),
            rhs.next};
  }

  ExpressionParseResult parseConditionalExpressionAt(ast::TreeBuilder& builder, size_t start,
                                                     size_t limit) const {
    ExpressionParseResult cond = parseErrorDefaultExpressionAt(builder, start, limit);
    if (!cond.node) { return cond; }
    if (cond.next >= limit || kindAt(cond.next) != ast::SyntaxKind::Question) { return cond; }

    const size_t question = cond.next;
    ExpressionParseResult thenExpr = parseAssignmentExpressionAt(builder, question + 1, limit);
    if (!thenExpr.node) {
      diagnoseExpressionExpected(question + 1);
      return ExpressionParseResult();
    }
    if (thenExpr.next >= limit || kindAt(thenExpr.next) != ast::SyntaxKind::Colon) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(thenExpr.next),
                                                                    ":"_zc);
      return ExpressionParseResult();
    }

    ExpressionParseResult elseExpr = parseAssignmentExpressionAt(builder, thenExpr.next + 1, limit);
    if (!elseExpr.node) {
      diagnoseExpressionExpected(thenExpr.next + 1);
      return ExpressionParseResult();
    }

    ast::NodePayload payload;
    writeNode(payload, ast::kConditionalExprCondWord, cond.node);
    writeNode(payload, ast::kConditionalExprThenExprWord, thenExpr.node);
    writeNode(payload, ast::kConditionalExprElseExprWord, elseExpr.node);
    return {
        builder.makeNode(ast::SyntaxKind::ConditionalExpr, rangeFor(start, elseExpr.next), payload),
        elseExpr.next};
  }

  ExpressionParseResult parseErrorDefaultExpressionAt(ast::TreeBuilder& builder, size_t start,
                                                      size_t limit) const {
    ExpressionParseResult primary = parseNullCoalesceExpressionAt(builder, start, limit);
    if (!primary.node) { return primary; }
    if (primary.next >= limit || kindAt(primary.next) != ast::SyntaxKind::ErrorDefault) {
      return primary;
    }

    const size_t opIndex = primary.next;
    ExpressionParseResult fallback = parseErrorDefaultExpressionAt(builder, opIndex + 1, limit);
    if (!fallback.node) {
      diagnoseExpressionExpected(opIndex + 1);
      return ExpressionParseResult();
    }

    ast::NodePayload payload;
    writeNode(payload, ast::kErrorDefaultExprPrimaryWord, primary.node);
    writeNode(payload, ast::kErrorDefaultExprFallbackWord, fallback.node);
    return {builder.makeNode(ast::SyntaxKind::ErrorDefaultExpr, rangeFor(start, fallback.next),
                             payload),
            fallback.next};
  }

  ExpressionParseResult parseNullCoalesceExpressionAt(ast::TreeBuilder& builder, size_t start,
                                                      size_t limit) const {
    ExpressionParseResult primary = parseBinaryExpressionAt(builder, start, limit, 1);
    if (!primary.node) { return primary; }
    if (primary.next >= limit || kindAt(primary.next) != ast::SyntaxKind::QuestionQuestion) {
      return primary;
    }

    const size_t opIndex = primary.next;
    ExpressionParseResult fallback = parseNullCoalesceExpressionAt(builder, opIndex + 1, limit);
    if (!fallback.node) {
      diagnoseExpressionExpected(opIndex + 1);
      return ExpressionParseResult();
    }

    ast::NodePayload payload;
    writeNode(payload, ast::kNullCoalesceExprPrimaryWord, primary.node);
    writeNode(payload, ast::kNullCoalesceExprFallbackWord, fallback.node);
    return {builder.makeNode(ast::SyntaxKind::NullCoalesceExpr, rangeFor(start, fallback.next),
                             payload),
            fallback.next};
  }

  ExpressionParseResult parseBinaryExpressionAt(ast::TreeBuilder& builder, size_t start,
                                                size_t limit, int32_t minPrecedence) const {
    ExpressionParseResult lhs = parseUnaryExpressionAt(builder, start, limit);
    if (!lhs.node) { return lhs; }

    size_t cursor = lhs.next;
    while (cursor < limit) {
      const ast::SyntaxKind kind = kindAt(cursor);
      const int32_t precedence = kind == ast::SyntaxKind::AsKeyword
                                     ? binaryPrecedence(ast::SyntaxKind::IsKeyword)
                                     : binaryPrecedence(kind);
      if (precedence < minPrecedence) { break; }

      if (kind == ast::SyntaxKind::AsKeyword) {
        size_t typeStart = cursor + 1;
        uint8_t mode = castModeCode(ast::SyntaxKind::AsKeyword);
        if (typeStart < limit && kindAt(typeStart) == ast::SyntaxKind::Exclamation) {
          diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
              tokenAt(typeStart).getLocation());
          return ExpressionParseResult();
        }
        if (typeStart < limit && kindAt(typeStart) == ast::SyntaxKind::Question) {
          mode = castModeCode(kindAt(typeStart));
          ++typeStart;
        }

        TypeParseResult ty = parseTypeExpressionAt(builder, typeStart, limit);
        if (!ty.node) {
          diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(typeStart));
          return ExpressionParseResult();
        }
        if (isStandaloneDynTypeRange(typeStart, ty.next)) {
          diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
              tokenAt(typeStart).getLocation());
          return ExpressionParseResult();
        }

        ast::NodePayload payload;
        payload.words[ast::kCastExpressionModeWord] = mode;
        writeNode(payload, ast::kCastExpressionExprWord, lhs.node);
        writeNode(payload, ast::kCastExpressionTyWord, ty.node);
        lhs = {builder.makeNode(ast::SyntaxKind::CastExpression, rangeFor(start, ty.next), payload),
               ty.next};
        cursor = lhs.next;
        continue;
      }

      if (kind == ast::SyntaxKind::IsKeyword) {
        TypeParseResult ty = parseTypeExpressionAt(builder, cursor + 1, limit);
        if (!ty.node) {
          diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(cursor + 1));
          return ExpressionParseResult();
        }

        ast::NodePayload payload;
        writeNode(payload, ast::kIsExpressionExprWord, lhs.node);
        writeNode(payload, ast::kIsExpressionTyWord, ty.node);
        lhs = {builder.makeNode(ast::SyntaxKind::IsExpression, rangeFor(start, ty.next), payload),
               ty.next};
        cursor = lhs.next;
        continue;
      }

      const bool rightAssociative = kind == ast::SyntaxKind::AsteriskAsterisk;
      ExpressionParseResult rhs = parseBinaryExpressionAt(
          builder, cursor + 1, limit, rightAssociative ? precedence : precedence + 1);
      if (!rhs.node) {
        diagnoseExpressionExpected(cursor + 1);
        return ExpressionParseResult();
      }

      ast::NodePayload payload;
      payload.words[ast::kBinaryExprOpWord] = binaryOpCode(kind);
      writeNode(payload, ast::kBinaryExprLhsWord, lhs.node);
      writeNode(payload, ast::kBinaryExprRhsWord, rhs.node);
      lhs = {builder.makeNode(ast::SyntaxKind::BinaryExpr, rangeFor(start, rhs.next), payload),
             rhs.next};
      cursor = lhs.next;
    }

    return lhs;
  }

  ExpressionParseResult parseUnaryExpressionAt(ast::TreeBuilder& builder, size_t start,
                                               size_t limit) const {
    if (start >= limit) { return ExpressionParseResult(); }

    if (kindAt(start) == ast::SyntaxKind::TypeOfKeyword) {
      ExpressionParseResult operand = parseUnaryExpressionAt(builder, start + 1, limit);
      if (!operand.node) {
        diagnoseExpressionExpected(start + 1);
        return ExpressionParseResult();
      }
      ast::NodePayload payload;
      writeNode(payload, ast::kTypeOfExpressionExprWord, operand.node);
      return {builder.makeNode(ast::SyntaxKind::TypeOfExpression, rangeFor(start, operand.next),
                               payload),
              operand.next};
    }

    if (isPrefixUnaryOperator(kindAt(start))) {
      ExpressionParseResult operand = parseUnaryExpressionAt(builder, start + 1, limit);
      if (!operand.node) {
        diagnoseExpressionExpected(start + 1);
        return ExpressionParseResult();
      }
      ast::NodePayload payload;
      payload.words[ast::kUnaryExpressionOpWord] = unaryOpCode(kindAt(start));
      writeNode(payload, ast::kUnaryExpressionOperandWord, operand.node);
      return {builder.makeNode(ast::SyntaxKind::UnaryExpression, rangeFor(start, operand.next),
                               payload),
              operand.next};
    }

    return parsePostfixExpressionAt(builder, start, limit);
  }

  ExpressionParseResult parsePostfixExpressionAt(ast::TreeBuilder& builder, size_t start,
                                                 size_t limit) const {
    ExpressionParseResult current = parsePrimaryExpressionAt(builder, start, limit);
    if (!current.node) { return current; }

    size_t cursor = current.next;
    while (cursor < limit) {
      const ast::SyntaxKind kind = kindAt(cursor);
      if (isPostfixOperator(kind)) {
        if ((kind == ast::SyntaxKind::PlusPlus || kind == ast::SyntaxKind::MinusMinus) &&
            isDefinitelyNonLValueOperand(start, cursor)) {
          diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
              tokenAt(cursor).getLocation());
          return ExpressionParseResult();
        }

        ast::NodePayload payload;
        payload.words[ast::kPostfixExpressionOpWord] = postfixOpCode(kind);
        writeNode(payload, ast::kPostfixExpressionOperandWord, current.node);
        current = {builder.makeNode(ast::SyntaxKind::PostfixExpression, rangeFor(start, cursor + 1),
                                    payload),
                   cursor + 1};
        cursor = current.next;
        continue;
      }

      if (kind == ast::SyntaxKind::RaisesKeyword && cursor + 1 < limit &&
          kindAt(cursor + 1) == ast::SyntaxKind::Question) {
        ast::NodePayload payload;
        payload.words[ast::kPostfixExpressionOpWord] =
            static_cast<uint8_t>(ast::PostfixOperatorKind::ErrorPropagate);
        writeNode(payload, ast::kPostfixExpressionOperandWord, current.node);
        current = {builder.makeNode(ast::SyntaxKind::PostfixExpression, rangeFor(start, cursor + 2),
                                    payload),
                   cursor + 2};
        cursor = current.next;
        continue;
      }

      if (kind == ast::SyntaxKind::LessThan) {
        const size_t closeAngle = findMatchingAngleClose(cursor, limit);
        if (closeAngle < limit && closeAngle + 1 < limit &&
            kindAt(closeAngle + 1) == ast::SyntaxKind::LeftBrace &&
            isStructLiteralTypeReference(start, closeAngle + 1)) {
          const size_t closeBrace = findMatchingRightBrace(closeAngle + 1, limit);
          if (closeBrace >= limit) {
            diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(
                diagnosticLoc(closeAngle + 1), "}"_zc);
            return ExpressionParseResult();
          }

          current = {parseStructLiteralExpression(builder, start, closeAngle + 1, closeBrace + 1),
                     closeBrace + 1};
          cursor = current.next;
          continue;
        }
        if (closeAngle < limit && closeAngle + 1 < limit &&
            kindAt(closeAngle + 1) == ast::SyntaxKind::LeftParen) {
          const size_t closeParen = findMatchingRightParen(closeAngle + 1, limit);
          if (closeParen >= limit) {
            diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(
                diagnosticLoc(closeAngle + 1), ")"_zc);
            return ExpressionParseResult();
          }

          ast::NodePayload payload;
          writeNode(payload, ast::kCallExpressionCalleeWord, current.node);
          writeNodeList(payload, ast::kCallExpressionTypeArgsFirstWord,
                        ast::kCallExpressionTypeArgsSizeWord,
                        parseTypeArgumentListRange(builder, cursor + 1, closeAngle));
          writeNodeList(payload, ast::kCallExpressionArgsFirstWord,
                        ast::kCallExpressionArgsSizeWord,
                        parseExpressionArguments(builder, closeAngle + 2, closeParen));
          current = {builder.makeNode(ast::SyntaxKind::CallExpression,
                                      rangeFor(start, closeParen + 1), payload),
                     closeParen + 1};
          cursor = current.next;
          continue;
        }
      }

      size_t callOpen = cursor;
      if (kind == ast::SyntaxKind::QuestionDot && cursor + 1 < limit &&
          kindAt(cursor + 1) == ast::SyntaxKind::LeftParen) {
        callOpen = cursor + 1;
      }
      if (kind == ast::SyntaxKind::LeftParen || callOpen != cursor) {
        const size_t closeParen = findMatchingRightParen(callOpen, limit);
        if (closeParen >= limit) {
          diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(callOpen),
                                                                        ")"_zc);
          return ExpressionParseResult();
        }

        ast::NodePayload payload;
        writeNode(payload, ast::kCallExpressionCalleeWord, current.node);
        writeNodeList(payload, ast::kCallExpressionArgsFirstWord, ast::kCallExpressionArgsSizeWord,
                      parseExpressionArguments(builder, callOpen + 1, closeParen));
        current = {builder.makeNode(ast::SyntaxKind::CallExpression,
                                    rangeFor(start, closeParen + 1), payload),
                   closeParen + 1};
        cursor = current.next;
        continue;
      }

      size_t indexOpen = cursor;
      if (kind == ast::SyntaxKind::QuestionDot && cursor + 1 < limit &&
          kindAt(cursor + 1) == ast::SyntaxKind::LeftBracket) {
        indexOpen = cursor + 1;
      }
      if (kind == ast::SyntaxKind::LeftBracket || indexOpen != cursor) {
        const size_t closeBracket = findMatchingRightBracket(indexOpen, limit);
        if (closeBracket >= limit) {
          diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(indexOpen),
                                                                        "]"_zc);
          return ExpressionParseResult();
        }
        const ast::NodeId index = parseExpressionRange(builder, indexOpen + 1, closeBracket);
        if (!index) { return ExpressionParseResult(); }

        ast::NodePayload payload;
        writeNode(payload, ast::kIndexExpressionObjectWord, current.node);
        writeNode(payload, ast::kIndexExpressionIndexWord, index);
        current = {builder.makeNode(ast::SyntaxKind::IndexExpression,
                                    rangeFor(start, closeBracket + 1), payload),
                   closeBracket + 1};
        cursor = current.next;
        continue;
      }

      if (kind == ast::SyntaxKind::Period ||
          (kind == ast::SyntaxKind::QuestionDot && cursor + 1 < limit &&
           isPropertyNameLike(kindAt(cursor + 1)))) {
        if (cursor + 1 >= limit || !isPropertyNameLike(kindAt(cursor + 1))) {
          diagnosticEngine.diagnose<diagnostics::DiagID::IdentifierExpected>(
              diagnosticLoc(cursor + 1));
          return ExpressionParseResult();
        }

        ast::NodePayload payload;
        writeNode(payload, ast::kMemberExpressionObjectWord, current.node);
        writeIdent(payload, ast::kMemberExpressionPropertyWord, internIdent(builder, cursor + 1));
        current = {builder.makeNode(ast::SyntaxKind::MemberExpression, rangeFor(start, cursor + 2),
                                    payload),
                   cursor + 2};
        cursor = current.next;
        continue;
      }

      if (kind == ast::SyntaxKind::LeftBrace && isStructLiteralTypeReference(start, cursor)) {
        const size_t closeBrace = findMatchingRightBrace(cursor, limit);
        if (closeBrace >= limit) {
          diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(cursor),
                                                                        "}"_zc);
          return ExpressionParseResult();
        }
        current = {parseStructLiteralExpression(builder, start, cursor, closeBrace + 1),
                   closeBrace + 1};
        if (!current.node) { return current; }
        cursor = current.next;
        continue;
      }

      break;
    }

    return current;
  }

  ExpressionParseResult parsePrimaryExpressionAt(ast::TreeBuilder& builder, size_t start,
                                                 size_t limit) const {
    if (start >= limit) { return ExpressionParseResult(); }

    if (kindAt(start) == ast::SyntaxKind::LeftParen) {
      const ast::NodeId lambda = parseLambdaExpression(builder, start, limit);
      if (lambda) { return {lambda, limit}; }

      const size_t closeParen = findMatchingRightParen(start, limit);
      if (closeParen >= limit) {
        diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(start), ")"_zc);
        return ExpressionParseResult();
      }

      if (start + 1 == closeParen) {
        zc::Vector<ast::NodeId> elems;
        ast::NodePayload payload;
        writeNodeList(payload, ast::kTupleLiteralElemsFirstWord, ast::kTupleLiteralElemsSizeWord,
                      builder.makeList(elems.asPtr()));
        return {builder.makeNode(ast::SyntaxKind::TupleLiteral, rangeFor(start, closeParen + 1),
                                 payload),
                closeParen + 1};
      }

      if (findTopLevelToken(start + 1, closeParen, ast::SyntaxKind::Comma) < closeParen) {
        return {parseExpressionList(builder, start + 1, closeParen, ast::SyntaxKind::TupleLiteral),
                closeParen + 1};
      }

      ExpressionParseResult inner = parseExpressionAt(builder, start + 1, closeParen);
      if (!inner.node) { return inner; }
      if (inner.next != closeParen) {
        diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
            diagnosticLoc(inner.next));
        return ExpressionParseResult();
      }
      return {inner.node, closeParen + 1};
    }

    if (kindAt(start) == ast::SyntaxKind::LeftBracket) {
      const size_t closeBracket = findMatchingRightBracket(start, limit);
      if (closeBracket >= limit) {
        diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(start), "]"_zc);
        return ExpressionParseResult();
      }
      return {parseExpressionList(builder, start + 1, closeBracket, ast::SyntaxKind::ArrayLiteral),
              closeBracket + 1};
    }

    if (kindAt(start) == ast::SyntaxKind::LeftBrace) {
      const size_t closeBrace = findMatchingRightBrace(start, limit);
      if (closeBrace >= limit) {
        diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(start), "}"_zc);
        return ExpressionParseResult();
      }
      return {parseObjectLiteralExpression(builder, start, closeBrace + 1), closeBrace + 1};
    }

    if (kindAt(start) == ast::SyntaxKind::TemplateHead) {
      const size_t tail = findTopLevelToken(start + 1, limit, ast::SyntaxKind::TemplateTail);
      const size_t end = tail < limit ? tail + 1 : limit;
      return {parseTemplateLiteralExpression(builder, start, end), end};
    }

    if (kindAt(start) == ast::SyntaxKind::SpawnKeyword) {
      return {parseSpawnExpression(builder, start, limit), limit};
    }

    if (isSoftKeyword(start, "unsafe"_zc) && start + 1 < limit &&
        kindAt(start + 1) == ast::SyntaxKind::LeftBrace) {
      const size_t closeBrace = findMatchingRightBrace(start + 1, limit);
      if (closeBrace >= limit) {
        diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(start + 1),
                                                                      "}"_zc);
        return ExpressionParseResult();
      }
      return {parseUnsafeBlockExpression(builder, start, closeBrace + 1), closeBrace + 1};
    }

    if (kindAt(start) == ast::SyntaxKind::FunKeyword) {
      return {parseFunctionExpression(builder, start, limit), limit};
    }

    if (isMacroInvocationStart(start, limit)) {
      const size_t macroEnd = findMacroInvocationEnd(start, limit);
      if (macroEnd >= limit && findMatchingMacroGroup(start + 2, limit) >= limit) {
        diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(
            diagnosticLoc(start + 2), macroGroupCloseLabel(kindAt(start + 2)));
        return ExpressionParseResult();
      }
      return {parseMacroInvocationExpression(builder, start, macroEnd), macroEnd};
    }

    if (kindAt(start) == ast::SyntaxKind::NewKeyword) {
      size_t end = start + 1;
      if (end < limit) {
        end = findTypePathEnd(end, limit);
        if (end == start + 1) { end = start + 2; }
        if (end < limit && kindAt(end) == ast::SyntaxKind::LessThan) {
          const size_t closeAngle = findMatchingAngleClose(end, limit);
          if (closeAngle < limit) { end = closeAngle + 1; }
        }
        if (end < limit && kindAt(end) == ast::SyntaxKind::LeftParen) {
          const size_t closeParen = findMatchingRightParen(end, limit);
          if (closeParen < limit) { end = closeParen + 1; }
        }
      }
      return {parseNewExpression(builder, start, end), end};
    }

    if (kindAt(start) == ast::SyntaxKind::ImportKeyword) {
      if (start + 1 < limit && kindAt(start + 1) == ast::SyntaxKind::LeftParen) {
        const size_t closeParen = findMatchingRightParen(start + 1, limit);
        if (closeParen < limit) {
          return {parseImportCallExpression(builder, start, closeParen + 1), closeParen + 1};
        }
      }
      return {parseImportCallExpression(builder, start, start + 1), start + 1};
    }

    if (start + 1 <= limit) {
      ast::NodePayload payload;
      switch (kindAt(start)) {
        case ast::SyntaxKind::Identifier:
          if (tokenAt(start).getValue() == "_"_zc) { return ExpressionParseResult(); }
          writeIdent(payload, ast::kIdentExprNameWord, internIdent(builder, start));
          return {builder.makeNode(ast::SyntaxKind::IdentExpr, rangeFor(start, start + 1), payload),
                  start + 1};
        case ast::SyntaxKind::ThisKeyword:
          return {builder.makeNode(ast::SyntaxKind::ThisExpr, rangeFor(start, start + 1), payload),
                  start + 1};
        case ast::SyntaxKind::TrueKeyword:
        case ast::SyntaxKind::FalseKeyword:
          payload.words[ast::kBoolLiteralValueWord] =
              kindAt(start) == ast::SyntaxKind::TrueKeyword ? 1 : 0;
          return {
              builder.makeNode(ast::SyntaxKind::BoolLiteral, rangeFor(start, start + 1), payload),
              start + 1};
        case ast::SyntaxKind::NullKeyword:
          return {
              builder.makeNode(ast::SyntaxKind::NullLiteral, rangeFor(start, start + 1), payload),
              start + 1};
        case ast::SyntaxKind::UnitKeyword:
          return {
              builder.makeNode(ast::SyntaxKind::UnitLiteral, rangeFor(start, start + 1), payload),
              start + 1};
        case ast::SyntaxKind::IntegerLiteral:
          payload.words[ast::kIntLiteralBaseWord] = integerBase(tokenAt(start).getValue());
          writeBigInt(payload, ast::kIntLiteralValueWord,
                      builder.internBigInt(tokenAt(start).getValue()));
          return {
              builder.makeNode(ast::SyntaxKind::IntLiteral, rangeFor(start, start + 1), payload),
              start + 1};
        case ast::SyntaxKind::BigIntLiteralToken:
          writeBigInt(payload, ast::kBigIntLiteralValueWord,
                      builder.internBigInt(tokenAt(start).getValue()));
          return {
              builder.makeNode(ast::SyntaxKind::BigIntLiteral, rangeFor(start, start + 1), payload),
              start + 1};
        case ast::SyntaxKind::FloatLiteral:
          payload.words[ast::kFloatLiteralExprWidthWord] = 64;
          writeFloat(payload, ast::kFloatLiteralExprValueWord,
                     builder.internFloat(tokenAt(start).getValue()));
          return {builder.makeNode(ast::SyntaxKind::FloatLiteralExpr, rangeFor(start, start + 1),
                                   payload),
                  start + 1};
        case ast::SyntaxKind::StringLiteral:
        case ast::SyntaxKind::CharacterLiteral:
        case ast::SyntaxKind::NoSubstitutionTemplateLiteral:
          writeString(payload, ast::kStrLiteralValueWord, internString(builder, start));
          return {
              builder.makeNode(ast::SyntaxKind::StrLiteral, rangeFor(start, start + 1), payload),
              start + 1};
        default:
          break;
      }

      if (isExpressionIdentifierLike(kindAt(start))) {
        writeIdent(payload, ast::kIdentExprNameWord, internIdent(builder, start));
        return {builder.makeNode(ast::SyntaxKind::IdentExpr, rangeFor(start, start + 1), payload),
                start + 1};
      }
    }

    return ExpressionParseResult();
  }

  ast::NodeId parseExpressionRange(ast::TreeBuilder& builder, size_t start, size_t end) const {
    while (start < end && kindAt(end - 1) == ast::SyntaxKind::Semicolon) { --end; }
    if (start >= end) { return ast::NodeId(); }

    if (isMacroInvocationStart(start, end)) {
      const size_t macroEnd = findMacroInvocationEnd(start, end);
      if (macroEnd == end) { return parseMacroInvocationExpression(builder, start, end); }
    }

    ExpressionParseResult parsed = parseExpressionAt(builder, start, end);
    if (!parsed.node) { return ast::NodeId(); }
    if (parsed.next != end) {
      diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
          diagnosticLoc(parsed.next));
      return ast::NodeId();
    }
    return parsed.node;
  }

  ast::NodeId parsePatternRange(ast::TreeBuilder& builder, size_t start, size_t end) const {
    if (start >= end) { return ast::NodeId(); }

    ast::NodePayload payload;
    const size_t at = findTopLevelToken(start, end, ast::SyntaxKind::At);
    if (at < end) {
      if (at == start || at + 1 >= end ||
          findTopLevelToken(at + 1, end, ast::SyntaxKind::At) < end) {
        diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
            tokenAt(at).getLocation());
        return ast::NodeId();
      }
      if (kindAt(start) != ast::SyntaxKind::Identifier || tokenAt(start).getValue() == "_"_zc) {
        diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
            tokenAt(start).getLocation());
        return ast::NodeId();
      }
      ast::NodePayload bindingPayload;
      writeIdent(bindingPayload, ast::kBindingPatternNameWord, internIdent(builder, start));
      writeNode(bindingPayload, ast::kBindingPatternSubWord,
                parsePatternRange(builder, at + 1, end));
      return builder.makeNode(ast::SyntaxKind::BindingPattern, rangeFor(start, end),
                              bindingPayload);
    }

    if (kindAt(start) == ast::SyntaxKind::IsKeyword) {
      if (start + 1 >= end) {
        diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(end));
        return ast::NodeId();
      }
      const ast::NodeId ty = parseTypeRange(builder, start + 1, end);
      if (!ty) {
        diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(start + 1));
        return ast::NodeId();
      }
      writeNode(payload, ast::kIsPatternTyWord, ty);
      return builder.makeNode(ast::SyntaxKind::IsPattern, rangeFor(start, end), payload);
    }

    if (rangeIsWrapped(start, end, ast::SyntaxKind::LeftParen, ast::SyntaxKind::RightParen)) {
      const bool hasComma = findTopLevelToken(start + 1, end - 1, ast::SyntaxKind::Comma) < end - 1;
      if (start + 1 < end - 1 && !hasComma && kindAt(start + 1) != ast::SyntaxKind::DotDotDot) {
        const ast::NodeId expr = parseExpressionRange(builder, start + 1, end - 1);
        if (expr) {
          writeNode(payload, ast::kExpressionPatternExprWord, expr);
          return builder.makeNode(ast::SyntaxKind::ExpressionPattern, rangeFor(start, end),
                                  payload);
        }
        return parsePatternRange(builder, start + 1, end - 1);
      }

      zc::Vector<ast::NodeId> pats;
      const size_t listEnd = end - 1;
      TokenCursor cursor = tokenCursorAt(start + 1);
      while (cursor.position() < listEnd) {
        const size_t itemStart = cursor.position();
        const size_t itemEnd = consumeCommaDelimitedItem(cursor, listEnd);
        if (itemStart < itemEnd) {
          if (kindAt(itemStart) == ast::SyntaxKind::DotDotDot) {
            if (cursor.position() < listEnd) {
              diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
                  tokenAt(itemStart).getLocation());
              return ast::NodeId();
            }
            ast::NodePayload restPayload;
            if (itemStart + 1 < itemEnd && kindAt(itemStart + 1) == ast::SyntaxKind::Identifier) {
              writeIdent(restPayload, ast::kRestPatternBindingWord,
                         internIdent(builder, itemStart + 1));
            }
            pats.add(builder.makeNode(ast::SyntaxKind::RestPattern, rangeFor(itemStart, itemEnd),
                                      restPayload));
          } else {
            addNodeIfPresent(pats, parsePatternRange(builder, itemStart, itemEnd));
          }
        }
        if (cursor.position() < listEnd && cursor.peek() == ast::SyntaxKind::Comma) {
          cursor.advance();
        }
      }
      writeNodeList(payload, ast::kTuplePatternPatsFirstWord, ast::kTuplePatternPatsSizeWord,
                    builder.makeList(pats.asPtr()));
      return builder.makeNode(ast::SyntaxKind::TuplePattern, rangeFor(start, end), payload);
    }

    if (rangeIsWrapped(start, end, ast::SyntaxKind::LeftBracket, ast::SyntaxKind::RightBracket)) {
      zc::Vector<ast::NodeId> pats;
      ast::NodeId rest;
      const size_t listEnd = end - 1;
      TokenCursor cursor = tokenCursorAt(start + 1);
      while (cursor.position() < listEnd) {
        const size_t itemStart = cursor.position();
        const size_t itemEnd = consumeCommaDelimitedItem(cursor, listEnd);
        if (itemStart < itemEnd) {
          if (kindAt(itemStart) == ast::SyntaxKind::DotDotDot) {
            if (cursor.position() < listEnd) {
              diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
                  tokenAt(itemStart).getLocation());
              return ast::NodeId();
            }
            ast::NodePayload restPayload;
            if (itemStart + 1 < itemEnd && kindAt(itemStart + 1) == ast::SyntaxKind::Identifier) {
              writeIdent(restPayload, ast::kRestPatternBindingWord,
                         internIdent(builder, itemStart + 1));
            }
            rest = builder.makeNode(ast::SyntaxKind::RestPattern, rangeFor(itemStart, itemEnd),
                                    restPayload);
          } else {
            addNodeIfPresent(pats, parsePatternRange(builder, itemStart, itemEnd));
          }
        }
        if (cursor.position() < listEnd && cursor.peek() == ast::SyntaxKind::Comma) {
          cursor.advance();
        }
      }
      writeNodeList(payload, ast::kArrayPatternPatsFirstWord, ast::kArrayPatternPatsSizeWord,
                    builder.makeList(pats.asPtr()));
      writeNode(payload, ast::kArrayPatternRestWord, rest);
      return builder.makeNode(ast::SyntaxKind::ArrayPattern, rangeFor(start, end), payload);
    }

    const size_t enumArgsOpen = findTrailingCallOpen(start, end);
    if (enumArgsOpen < end && kindAt(start) == ast::SyntaxKind::Identifier) {
      zc::Vector<ast::NodeId> args;
      const size_t listEnd = end - 1;
      TokenCursor cursor = tokenCursorAt(enumArgsOpen + 1);
      while (cursor.position() < listEnd) {
        const size_t itemStart = cursor.position();
        const size_t itemEnd = consumeCommaDelimitedItem(cursor, listEnd);
        if (itemStart < itemEnd) {
          addNodeIfPresent(args, parsePatternRange(builder, itemStart, itemEnd));
        }
        if (cursor.position() < listEnd && cursor.peek() == ast::SyntaxKind::Comma) {
          cursor.advance();
        }
      }
      ast::NodePayload enumPayload;
      writeNode(enumPayload, ast::kEnumPatternPathWord,
                makeModulePath(builder, start, enumArgsOpen));
      writeNodeList(enumPayload, ast::kEnumPatternArgsFirstWord, ast::kEnumPatternArgsSizeWord,
                    builder.makeList(args.asPtr()));
      return builder.makeNode(ast::SyntaxKind::EnumPattern, rangeFor(start, end), enumPayload);
    }

    size_t structStart = start;
    ast::NodeId structTyPath;
    if (kindAt(start) == ast::SyntaxKind::Identifier) {
      const size_t brace = findTopLevelToken(start + 1, end, ast::SyntaxKind::LeftBrace);
      if (brace < end &&
          rangeIsWrapped(brace, end, ast::SyntaxKind::LeftBrace, ast::SyntaxKind::RightBrace)) {
        structTyPath = makeModulePath(builder, start, brace);
        structStart = brace;
      }
    }

    if (rangeIsWrapped(structStart, end, ast::SyntaxKind::LeftBrace, ast::SyntaxKind::RightBrace)) {
      zc::Vector<ast::NodeId> properties;
      ast::NodeId rest;
      const size_t listEnd = end - 1;
      TokenCursor cursor = tokenCursorAt(structStart + 1);
      while (cursor.position() < listEnd) {
        const size_t itemStart = cursor.position();
        const size_t itemEnd = consumeCommaDelimitedItem(cursor, listEnd);
        if (itemStart < itemEnd) {
          if (kindAt(itemStart) == ast::SyntaxKind::DotDotDot) {
            if (cursor.position() < listEnd) {
              diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
                  tokenAt(itemStart).getLocation());
              return ast::NodeId();
            }
            ast::NodePayload restPayload;
            if (itemStart + 1 < itemEnd && kindAt(itemStart + 1) == ast::SyntaxKind::Identifier) {
              writeIdent(restPayload, ast::kRestPatternBindingWord,
                         internIdent(builder, itemStart + 1));
            }
            rest = builder.makeNode(ast::SyntaxKind::RestPattern, rangeFor(itemStart, itemEnd),
                                    restPayload);
          } else {
            const size_t colon = findTopLevelToken(itemStart, itemEnd, ast::SyntaxKind::Colon);
            if (kindAt(itemStart) != ast::SyntaxKind::Identifier) {
              diagnosticEngine.diagnose<diagnostics::DiagID::IdentifierExpected>(
                  diagnosticLoc(itemStart));
              return ast::NodeId();
            }
            ast::NodePayload propertyPayload;
            writeIdent(propertyPayload, ast::kPatternPropertyNameWord,
                       internIdent(builder, itemStart));
            if (colon < itemEnd) {
              writeNode(propertyPayload, ast::kPatternPropertyPatWord,
                        parsePatternRange(builder, colon + 1, itemEnd));
            } else {
              propertyPayload.words[ast::kPatternPropertyShortFormWord] = 1;
            }
            properties.add(builder.makeNode(ast::SyntaxKind::PatternProperty,
                                            rangeFor(itemStart, itemEnd), propertyPayload));
          }
        }
        if (cursor.position() < listEnd && cursor.peek() == ast::SyntaxKind::Comma) {
          cursor.advance();
        }
      }

      writeNode(payload, ast::kStructPatternTyPathWord, structTyPath);
      writeNodeList(payload, ast::kStructPatternFieldsFirstWord, ast::kStructPatternFieldsSizeWord,
                    builder.makeList(properties.asPtr()));
      writeNode(payload, ast::kStructPatternRestWord, rest);
      return builder.makeNode(ast::SyntaxKind::StructPattern, rangeFor(start, end), payload);
    }

    if (end == start + 1 && kindAt(start) == ast::SyntaxKind::Identifier) {
      writeIdent(payload, ast::kIdentifierPatternNameWord, internIdent(builder, start));
      return builder.makeNode(ast::SyntaxKind::IdentifierPattern, rangeFor(start, end), payload);
    }

    if (end == start + 1 && kindAt(start) == ast::SyntaxKind::Underscore) {
      return builder.makeNode(ast::SyntaxKind::WildcardPattern, rangeFor(start, end), payload);
    }

    if (end == start + 1 && isLiteralPatternToken(kindAt(start))) {
      const ast::NodeId literal = parseExpressionRange(builder, start, end);
      if (!literal) { return ast::NodeId(); }
      writeNode(payload, ast::kLiteralPatternLiteralWord, literal);
      return builder.makeNode(ast::SyntaxKind::LiteralPattern, rangeFor(start, end), payload);
    }

    if (rangeIsWrapped(start, end, ast::SyntaxKind::LeftParen, ast::SyntaxKind::RightParen)) {
      const ast::NodeId expr = parseExpressionRange(builder, start + 1, end - 1);
      if (expr) {
        writeNode(payload, ast::kExpressionPatternExprWord, expr);
        return builder.makeNode(ast::SyntaxKind::ExpressionPattern, rangeFor(start, end), payload);
      }
    }

    return ast::NodeId();
  }

  ast::NodeId makeEmptyClassMemberList(ast::TreeBuilder& builder, source::SourceRange range) const {
    zc::Vector<ast::NodeId> members;
    ast::NodePayload payload;
    writeNodeList(payload, ast::kClassMemberListMembersFirstWord,
                  ast::kClassMemberListMembersSizeWord, builder.makeList(members.asPtr()));
    return builder.makeNode(ast::SyntaxKind::ClassMemberList, zc::mv(range), payload);
  }

  ast::NodeId makeEmptyEnumVariantList(ast::TreeBuilder& builder, source::SourceRange range) const {
    zc::Vector<ast::NodeId> variants;
    ast::NodePayload payload;
    writeNodeList(payload, ast::kEnumVariantListVariantsFirstWord,
                  ast::kEnumVariantListVariantsSizeWord, builder.makeList(variants.asPtr()));
    return builder.makeNode(ast::SyntaxKind::EnumVariantList, zc::mv(range), payload);
  }

  size_t recoverFunctionParameter(TokenCursor& cursor, size_t closeParen) const {
    while (cursor.position() < closeParen && cursor.peek() != ast::SyntaxKind::Comma) {
      cursor.advance();
    }
    return cursor.position();
  }

  ast::NodeId parseFunctionParameter(ast::TreeBuilder& builder, TokenCursor& cursor,
                                     size_t closeParen) const {
    size_t parameterStart = cursor.position();
    if (isOuterAttributeStart(parameterStart, closeParen)) {
      diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
          tokenAt(parameterStart).getLocation());
      parameterStart = skipOuterAttributePrefix(parameterStart, closeParen);
      cursor.moveTo(parameterStart);
    }

    if (parameterStart >= closeParen) { return ast::NodeId(); }

    const size_t nameIndex = cursor.position();
    if (cursor.peek() != ast::SyntaxKind::Identifier) {
      diagnosticEngine.diagnose<diagnostics::DiagID::IdentifierExpected>(
          tokenAt(nameIndex).getLocation());
      recoverFunctionParameter(cursor, closeParen);
      return ast::NodeId();
    }
    const ast::IdentId name = internIdent(builder, nameIndex);
    cursor.advance();

    if (cursor.position() >= closeParen || cursor.peek() != ast::SyntaxKind::Colon) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(
          diagnosticLoc(cursor.position()), ":"_zc);
      recoverFunctionParameter(cursor, closeParen);
      return ast::NodeId();
    }
    cursor.advance();

    const size_t typeStart = cursor.position();
    TypeParseResult ty = parseTypeExpression(builder, cursor, closeParen);
    if (!ty.node) {
      diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(typeStart));
      recoverFunctionParameter(cursor, closeParen);
      return ast::NodeId();
    }

    ast::NodePayload parameterPayload;
    writeIdent(parameterPayload, ast::kFunctionParameterDeclNameWord, name);
    writeNode(parameterPayload, ast::kFunctionParameterDeclTyWord, ty.node);

    if (cursor.position() < closeParen && cursor.peek() == ast::SyntaxKind::Equals) {
      cursor.advance();
      const size_t defaultStart = cursor.position();
      const size_t defaultEnd = consumeVariableInitializer(cursor, closeParen);
      if (defaultStart >= defaultEnd) {
        diagnoseExpressionExpected(defaultStart);
        return ast::NodeId();
      }
      writeNode(parameterPayload, ast::kFunctionParameterDeclDefaultWord,
                parseRequiredExpression(builder, defaultStart, defaultEnd));
    }

    return builder.makeNode(ast::SyntaxKind::FunctionParameterDecl,
                            rangeFor(parameterStart, cursor.position()), parameterPayload);
  }

  ast::NodeList parseFunctionParameterNodeList(ast::TreeBuilder& builder, size_t openParen,
                                               size_t closeParen) const {
    zc::Vector<ast::NodeId> parameters;
    if (openParen < closeParen && closeParen <= tokenCountWithoutEof()) {
      TokenCursor cursor = tokenCursorAt(openParen + 1);
      while (cursor.position() < closeParen) {
        if (cursor.peek() == ast::SyntaxKind::Comma) {
          cursor.advance();
          continue;
        }

        const size_t parameterStart = cursor.position();
        addNodeIfPresent(parameters, parseFunctionParameter(builder, cursor, closeParen));
        if (cursor.position() <= parameterStart) { cursor.moveTo(parameterStart + 1); }
        if (cursor.position() >= closeParen) { break; }

        if (cursor.peek() == ast::SyntaxKind::Comma) {
          cursor.advance();
          continue;
        }

        diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(
            diagnosticLoc(cursor.position()), ","_zc);
        recoverFunctionParameter(cursor, closeParen);
      }
    }
    return builder.makeList(parameters.asPtr());
  }

  ast::NodeId parseFunctionParameterList(ast::TreeBuilder& builder, size_t openParen,
                                         size_t closeParen) const {
    const ast::NodeList parameterList =
        parseFunctionParameterNodeList(builder, openParen, closeParen);
    ast::NodePayload payload;
    payload.words[ast::kFunctionParameterListNparamsWord] = parameterList.size;
    writeNodeList(payload, ast::kFunctionParameterListParamsFirstWord,
                  ast::kFunctionParameterListParamsSizeWord, parameterList);
    return builder.makeNode(ast::SyntaxKind::FunctionParameterList,
                            rangeFor(openParen, closeParen + 1), payload);
  }

  size_t consumeSimpleStatementEnd(size_t start, size_t limit) const {
    int32_t parenDepth = 0;
    int32_t bracketDepth = 0;
    int32_t braceDepth = 0;
    bool sawBrace = false;

    for (size_t index = start; index < limit; ++index) {
      const ast::SyntaxKind kind = kindAt(index);

      switch (kind) {
        case ast::SyntaxKind::LeftParen:
          ++parenDepth;
          break;
        case ast::SyntaxKind::RightParen:
          if (parenDepth > 0) { --parenDepth; }
          break;
        case ast::SyntaxKind::LeftBracket:
          ++bracketDepth;
          break;
        case ast::SyntaxKind::RightBracket:
          if (bracketDepth > 0) { --bracketDepth; }
          break;
        case ast::SyntaxKind::LeftBrace:
          ++braceDepth;
          sawBrace = true;
          break;
        case ast::SyntaxKind::RightBrace:
          if (braceDepth > 0) {
            --braceDepth;
          } else {
            emitUnexpected(tokenAt(index));
            return index + 1;
          }
          break;
        case ast::SyntaxKind::Semicolon:
          if (parenDepth == 0 && bracketDepth == 0 && braceDepth == 0) { return index + 1; }
          break;
        default:
          break;
      }
    }

    if (sawBrace && braceDepth > 0) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(
          tokenAt(limit - 1).getLocation(), "}"_zc);
    }
    return limit;
  }

  size_t consumeSpawnStatementEnd(size_t start, size_t limit) const {
    size_t cursor = start + 1;
    while (cursor < limit && kindAt(cursor) == ast::SyntaxKind::Identifier) {
      const zc::StringPtr modifier = tokenAt(cursor).getValue();
      if (modifier == "detached"_zc || modifier == "blocking"_zc) {
        ++cursor;
        continue;
      }
      if (modifier == "priority"_zc && cursor + 1 < limit &&
          kindAt(cursor + 1) == ast::SyntaxKind::LeftParen) {
        const size_t closeParen = findMatchingRightParen(cursor + 1, limit);
        if (closeParen >= limit) { return consumeSimpleStatementEnd(start, limit); }
        cursor = closeParen + 1;
        continue;
      }
      break;
    }

    if (cursor < limit && kindAt(cursor) == ast::SyntaxKind::LeftBrace) {
      const size_t bodyEnd = consumeBracedBodyEnd(cursor, limit);
      if (bodyEnd < limit && kindAt(bodyEnd) == ast::SyntaxKind::Semicolon) { return bodyEnd + 1; }
      return bodyEnd;
    }

    return consumeSimpleStatementEnd(start, limit);
  }

  size_t consumeExternDeclarationEnd(size_t start, size_t limit) const {
    size_t cursor = start;
    if (isSoftKeyword(cursor, "unsafe"_zc)) { ++cursor; }
    if (cursor < limit && isSoftKeyword(cursor, "extern"_zc)) { ++cursor; }
    if (cursor < limit && kindAt(cursor) == ast::SyntaxKind::StringLiteral) { ++cursor; }

    if (cursor < limit && kindAt(cursor) == ast::SyntaxKind::LeftBrace) {
      return consumeBracedBodyEnd(cursor, limit);
    }

    return consumeSimpleStatementEnd(start, limit);
  }

  size_t findBindingDeclarationRecoveryStart(size_t start, size_t limit) const {
    int32_t parenDepth = 0;
    int32_t bracketDepth = 0;
    int32_t braceDepth = 0;
    int32_t angleDepth = 0;
    bool inTypeAnnotation = false;
    bool inInitializer = false;
    for (size_t index = start + 1; index < limit; ++index) {
      const ast::SyntaxKind kind = kindAt(index);
      if (parenDepth == 0 && bracketDepth == 0 && braceDepth == 0 && angleDepth == 0) {
        if (kind == ast::SyntaxKind::Comma) {
          inTypeAnnotation = false;
          inInitializer = false;
        } else if (kind == ast::SyntaxKind::Equals) {
          inTypeAnnotation = false;
          inInitializer = true;
        } else if (kind == ast::SyntaxKind::Colon && !inInitializer) {
          inTypeAnnotation = true;
        }
      }

      if (parenDepth == 0 && bracketDepth == 0 && braceDepth == 0 && angleDepth == 0 &&
          !inTypeAnnotation && canStartStatementAfterBindingDeclaration(kind) &&
          !canContinueLetInitializerBefore(index)) {
        return index;
      }

      switch (kind) {
        case ast::SyntaxKind::LeftParen:
          ++parenDepth;
          break;
        case ast::SyntaxKind::RightParen:
          if (parenDepth > 0) { --parenDepth; }
          break;
        case ast::SyntaxKind::LeftBracket:
          ++bracketDepth;
          break;
        case ast::SyntaxKind::RightBracket:
          if (bracketDepth > 0) { --bracketDepth; }
          break;
        case ast::SyntaxKind::LeftBrace:
          ++braceDepth;
          break;
        case ast::SyntaxKind::RightBrace:
          if (braceDepth > 0) { --braceDepth; }
          break;
        case ast::SyntaxKind::LessThan:
          if (inTypeAnnotation) { ++angleDepth; }
          break;
        default:
          if (inTypeAnnotation) {
            const int32_t closeCount = typeAngleCloseCount(kind);
            if (closeCount > 0 && angleDepth > 0) {
              angleDepth = closeCount >= angleDepth ? 0 : angleDepth - closeCount;
            }
          }
          break;
      }
    }
    return limit;
  }

  size_t consumeBindingDeclarationEnd(size_t start, size_t limit) const {
    const size_t semicolon = findTopLevelToken(start + 1, limit, ast::SyntaxKind::Semicolon);
    if (semicolon < limit) { return semicolon + 1; }

    const size_t recoveryStart = findBindingDeclarationRecoveryStart(start, limit);
    if (recoveryStart < limit) {
      diagnosticEngine.diagnose<diagnostics::DiagID::MissingSemicolon>(
          tokenAt(recoveryStart).getLocation(), tokenLabel(tokenAt(recoveryStart)));
      return recoveryStart;
    }
    return consumeSimpleStatementEnd(start, limit);
  }

  size_t consumeBracedBodyEnd(size_t bodyOpen, size_t limit) const {
    if (bodyOpen >= limit || kindAt(bodyOpen) != ast::SyntaxKind::LeftBrace) {
      return consumeSimpleStatementEnd(bodyOpen, limit);
    }

    const size_t closeBrace = findMatchingRightBrace(bodyOpen, limit);
    if (closeBrace < limit) { return closeBrace + 1; }
    diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(tokenAt(limit - 1).getLocation(),
                                                                  "}"_zc);
    return limit;
  }

  size_t consumeStatementBodyEnd(size_t bodyStart, size_t limit) const {
    if (bodyStart >= limit) { return limit; }
    if (kindAt(bodyStart) == ast::SyntaxKind::LeftBrace) {
      return consumeBracedBodyEnd(bodyStart, limit);
    }
    TokenCursor cursor = tokenCursorAt(bodyStart);
    return consumeSourceElement(cursor, limit).end;
  }

  size_t consumeConditionBodyStart(size_t start, size_t limit) const {
    const size_t condStart = start + 1;
    if (condStart < limit && kindAt(condStart) == ast::SyntaxKind::LeftParen) {
      const size_t closeParen = findMatchingRightParen(condStart, limit);
      return closeParen < limit ? closeParen + 1 : limit;
    }
    const size_t bodyOpen = findTopLevelToken(condStart, limit, ast::SyntaxKind::LeftBrace);
    return bodyOpen < limit ? bodyOpen : limit;
  }

  struct IfStatementParts {
    size_t condStart = 0;
    size_t condEnd = 0;
    size_t thenStart = 0;
    size_t thenEnd = 0;
    size_t elseIndex = 0;
    size_t elseStart = 0;
    size_t end = 0;
  };

  IfStatementParts parseIfStatementParts(size_t start, size_t limit) const {
    IfStatementParts parts;
    parts.elseIndex = limit;
    parts.elseStart = limit;
    conditionRangeAfterKeyword(start, limit, parts.condStart, parts.condEnd, parts.thenStart);
    parts.thenEnd = consumeStatementBodyEnd(parts.thenStart, limit);
    parts.end = parts.thenEnd;
    if (parts.thenEnd >= limit || kindAt(parts.thenEnd) != ast::SyntaxKind::ElseKeyword) {
      return parts;
    }

    parts.elseIndex = parts.thenEnd;
    parts.elseStart = parts.elseIndex + 1;
    parts.end = parts.elseStart < limit && kindAt(parts.elseStart) == ast::SyntaxKind::IfKeyword
                    ? consumeIfStatementEnd(parts.elseStart, limit)
                    : consumeStatementBodyEnd(parts.elseStart, limit);
    return parts;
  }

  size_t consumeIfStatementEnd(size_t start, size_t limit) const {
    return parseIfStatementParts(start, limit).end;
  }

  struct WhileStatementParts {
    size_t condStart = 0;
    size_t condEnd = 0;
    size_t bodyStart = 0;
    size_t bodyEnd = 0;
    size_t end = 0;
  };

  WhileStatementParts parseWhileStatementParts(size_t start, size_t limit) const {
    WhileStatementParts parts;
    conditionRangeAfterKeyword(start, limit, parts.condStart, parts.condEnd, parts.bodyStart);
    parts.bodyEnd = consumeStatementBodyEnd(parts.bodyStart, limit);
    parts.end = parts.bodyEnd;
    return parts;
  }

  size_t consumeWhileStatementEnd(size_t start, size_t limit) const {
    return parseWhileStatementParts(start, limit).end;
  }

  struct MatchStatementParts {
    size_t scrutineeStart = 0;
    size_t scrutineeEnd = 0;
    size_t bodyOpen = 0;
    size_t bodyClose = 0;
    size_t end = 0;
  };

  MatchStatementParts parseMatchStatementParts(size_t start, size_t limit) const {
    MatchStatementParts parts;
    parts.bodyOpen = limit;
    parts.bodyClose = limit;
    parts.end = limit;
    conditionRangeAfterKeyword(start, limit, parts.scrutineeStart, parts.scrutineeEnd,
                               parts.bodyOpen);
    if (parts.bodyOpen < limit && kindAt(parts.bodyOpen) == ast::SyntaxKind::LeftBrace) {
      const size_t closeBrace = findMatchingRightBrace(parts.bodyOpen, limit);
      parts.bodyClose = closeBrace < limit ? closeBrace : limit;
      parts.end = closeBrace < limit ? closeBrace + 1 : limit;
    } else {
      parts.bodyClose = parts.bodyOpen;
      parts.end = parts.bodyOpen;
    }
    return parts;
  }

  size_t consumeMatchStatementEnd(size_t start, size_t limit) const {
    return parseMatchStatementParts(start, limit).end;
  }

  struct DoWhileStatementParts {
    size_t bodyStart = 0;
    size_t bodyEnd = 0;
    size_t whileIndex = 0;
    size_t condStart = 0;
    size_t condEnd = 0;
    size_t end = 0;
  };

  DoWhileStatementParts parseDoWhileStatementParts(size_t start, size_t limit) const {
    DoWhileStatementParts parts;
    parts.bodyStart = start + 1;
    parts.bodyEnd = consumeStatementBodyEnd(parts.bodyStart, limit);
    parts.whileIndex = parts.bodyEnd;
    parts.condStart = limit;
    parts.condEnd = limit;
    parts.end = parts.bodyEnd;
    if (parts.whileIndex >= limit || kindAt(parts.whileIndex) != ast::SyntaxKind::WhileKeyword) {
      return parts;
    }

    size_t conditionEnd = limit;
    conditionRangeAfterKeyword(parts.whileIndex, limit, parts.condStart, parts.condEnd,
                               conditionEnd);
    parts.end = conditionEnd;
    if (conditionEnd < limit && kindAt(conditionEnd) == ast::SyntaxKind::Semicolon) {
      parts.end = conditionEnd + 1;
    }
    return parts;
  }

  size_t consumeDoWhileStatementEnd(size_t start, size_t limit) const {
    return parseDoWhileStatementParts(start, limit).end;
  }

  size_t consumeLabeledStatementEnd(size_t start, size_t limit) const {
    if (start + 2 >= limit || kindAt(start + 1) != ast::SyntaxKind::Colon) {
      return consumeSimpleStatementEnd(start, limit);
    }
    return consumeStatementBodyEnd(start + 2, limit);
  }

  size_t consumeTypeLike(size_t start, size_t limit) const {
    if (start >= limit) { return start; }

    size_t cursor = start;
    switch (kindAt(cursor)) {
      case ast::SyntaxKind::LeftBrace: {
        const size_t closeBrace = findMatchingRightBrace(cursor, limit);
        cursor = closeBrace < limit ? closeBrace + 1 : limit;
        break;
      }
      case ast::SyntaxKind::LeftParen: {
        const size_t closeParen = findMatchingRightParen(cursor, limit);
        cursor = closeParen < limit ? closeParen + 1 : limit;
        break;
      }
      case ast::SyntaxKind::LeftBracket: {
        const size_t closeBracket = findMatchingRightBracket(cursor, limit);
        cursor = closeBracket < limit ? closeBracket + 1 : limit;
        break;
      }
      case ast::SyntaxKind::Identifier: {
        cursor = findTypePathEnd(cursor, limit);
        if (cursor == start) { return start; }
        break;
      }
      default:
        if (!isPrimitiveTypeKeyword(kindAt(cursor)) &&
            kindAt(cursor) != ast::SyntaxKind::ThisKeyword) {
          return start;
        }
        ++cursor;
        break;
    }

    while (cursor < limit) {
      if (kindAt(cursor) == ast::SyntaxKind::LessThan) {
        const size_t closeAngle = findMatchingAngleClose(cursor, limit);
        cursor = closeAngle < limit ? closeAngle + 1 : limit;
        continue;
      }
      if (kindAt(cursor) == ast::SyntaxKind::LeftBracket && cursor + 1 < limit &&
          kindAt(cursor + 1) == ast::SyntaxKind::RightBracket) {
        cursor += 2;
        continue;
      }
      if (kindAt(cursor) == ast::SyntaxKind::Question) {
        ++cursor;
        continue;
      }
      break;
    }

    return cursor;
  }

  size_t findFunctionBodyOpenAfterParams(size_t closeParen, size_t limit) const {
    size_t cursor = closeParen + 1;
    while (cursor < limit) {
      if (kindAt(cursor) == ast::SyntaxKind::Semicolon) { return limit; }
      if (kindAt(cursor) == ast::SyntaxKind::LeftBrace) { return cursor; }
      if (kindAt(cursor) == ast::SyntaxKind::Arrow ||
          kindAt(cursor) == ast::SyntaxKind::RaisesKeyword) {
        const size_t typeEnd = consumeTypeLike(cursor + 1, limit);
        if (typeEnd <= cursor + 1) {
          ++cursor;
        } else {
          cursor = typeEnd;
        }
        continue;
      }
      ++cursor;
    }

    return limit;
  }

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

  FunctionDeclarationParts parseFunctionDeclarationParts(size_t start, size_t limit) const {
    FunctionDeclarationParts parts;
    parts.nameIndex = limit;
    parts.openParen = limit;
    parts.closeParen = limit;
    parts.headerEnd = limit;
    parts.bodyOpen = limit;
    parts.end = limit;
    parts.arrow = limit;
    parts.raises = limit;

    for (size_t index = start + 1; index < limit; ++index) {
      if (kindAt(index) == ast::SyntaxKind::Identifier) {
        parts.nameIndex = index;
        break;
      }
    }

    size_t cursor = parts.nameIndex < limit ? parts.nameIndex + 1 : start + 1;
    if (cursor < limit && kindAt(cursor) == ast::SyntaxKind::LessThan) {
      const size_t closeAngle = findMatchingAngleClose(cursor, limit);
      cursor = closeAngle < limit ? closeAngle + 1 : limit;
    }
    if (cursor < limit && kindAt(cursor) == ast::SyntaxKind::LeftParen) {
      parts.openParen = cursor;
    } else {
      parts.openParen = findTopLevelToken(cursor, limit, ast::SyntaxKind::LeftParen);
    }
    if (parts.openParen >= limit) {
      parts.end = consumeSimpleStatementEnd(start, limit);
      return parts;
    }

    parts.closeParen = findMatchingRightParen(parts.openParen, limit);
    if (parts.closeParen >= limit) { return parts; }

    parts.bodyOpen = findFunctionBodyOpenAfterParams(parts.closeParen, limit);
    parts.headerEnd = parts.bodyOpen < limit ? parts.bodyOpen : limit;
    const size_t semi =
        findTopLevelToken(parts.closeParen + 1, parts.headerEnd, ast::SyntaxKind::Semicolon);
    if (semi < parts.headerEnd) {
      parts.headerEnd = semi;
      parts.end = semi + 1;
    } else {
      parts.end = parts.bodyOpen < limit ? consumeBracedBodyEnd(parts.bodyOpen, limit) : limit;
    }
    parts.arrow = findTopLevelToken(parts.closeParen + 1, parts.headerEnd, ast::SyntaxKind::Arrow);
    parts.raises =
        findTopLevelToken(parts.closeParen + 1, parts.headerEnd, ast::SyntaxKind::RaisesKeyword);
    return parts;
  }

  size_t consumeFunctionDeclarationEnd(size_t start, size_t limit) const {
    return parseFunctionDeclarationParts(start, limit).end;
  }

  size_t consumeExportDeclarationEnd(size_t start, size_t limit) const {
    size_t declarationHead = start + 1;
    while (declarationHead < limit && isDeclarationModifier(kindAt(declarationHead))) {
      ++declarationHead;
    }
    if (declarationHead < limit && (isDeclarationHead(kindAt(declarationHead)) ||
                                    isSoftDeclarationHead(declarationHead, limit))) {
      TokenCursor cursor = tokenCursorAt(declarationHead);
      return consumeSourceElement(cursor, limit).end;
    }
    return consumeSimpleStatementEnd(start, limit);
  }

  size_t consumeNamedTypeDeclarationEnd(size_t start, size_t limit) const {
    size_t cursor = start + 1;
    while (cursor < limit && kindAt(cursor) != ast::SyntaxKind::Identifier) { ++cursor; }
    if (cursor < limit) { ++cursor; }

    if (cursor < limit && kindAt(cursor) == ast::SyntaxKind::LessThan) {
      const size_t closeAngle = findMatchingAngleClose(cursor, limit);
      cursor = closeAngle < limit ? closeAngle + 1 : limit;
    }

    while (cursor < limit) {
      const ast::SyntaxKind kind = kindAt(cursor);
      if (kind == ast::SyntaxKind::LeftBrace) { return consumeBracedBodyEnd(cursor, limit); }
      if (kind == ast::SyntaxKind::Semicolon) { return cursor + 1; }

      if (kind == ast::SyntaxKind::ExtendsKeyword || kind == ast::SyntaxKind::ImplementsKeyword) {
        ++cursor;
        while (cursor < limit) {
          const size_t typeEnd = consumeTypeLike(cursor, limit);
          if (typeEnd <= cursor) { break; }
          cursor = typeEnd;
          if (cursor < limit && kindAt(cursor) == ast::SyntaxKind::Comma) {
            ++cursor;
            continue;
          }
          break;
        }
        continue;
      }

      ++cursor;
    }

    return limit;
  }

  size_t consumeBracedDeclarationEnd(size_t start, size_t limit) const {
    const size_t bodyOpen = findTopLevelToken(start + 1, limit, ast::SyntaxKind::LeftBrace);
    const size_t semi = findTopLevelToken(start + 1, limit, ast::SyntaxKind::Semicolon);
    if (bodyOpen < limit && (semi >= limit || bodyOpen < semi)) {
      return consumeBracedBodyEnd(bodyOpen, limit);
    }
    return semi < limit ? semi + 1 : limit;
  }

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

  ForStatementParts parseForStatementParts(size_t head, size_t limit) const {
    ForStatementParts parts;
    parts.headerStart = head + 1;
    parts.headerEnd = limit;
    parts.bodyStart = limit;
    parts.bodyEnd = limit;
    parts.end = limit;

    if (parts.headerStart < limit && kindAt(parts.headerStart) == ast::SyntaxKind::LeftParen) {
      const size_t closeParen = findMatchingRightParen(parts.headerStart, limit);
      if (closeParen < limit) {
        ++parts.headerStart;
        parts.headerEnd = closeParen;
        parts.bodyStart = closeParen + 1;
      }
    } else {
      const size_t bodyOpen =
          findTopLevelToken(parts.headerStart, limit, ast::SyntaxKind::LeftBrace);
      if (bodyOpen < limit) {
        parts.headerEnd = bodyOpen;
        parts.bodyStart = bodyOpen;
      }
    }
    parts.firstSemi = parts.headerEnd;
    parts.secondSemi = parts.headerEnd;
    parts.inIndex = parts.headerEnd;

    TokenCursor cursor = tokenCursorAt(parts.headerStart);
    int32_t parenDepth = 0;
    int32_t bracketDepth = 0;
    int32_t braceDepth = 0;
    while (cursor.position() < parts.headerEnd) {
      const ast::SyntaxKind kind = cursor.peek();
      if (parenDepth == 0 && bracketDepth == 0 && braceDepth == 0) {
        if (kind == ast::SyntaxKind::Semicolon) {
          if (parts.firstSemi == parts.headerEnd) {
            parts.firstSemi = cursor.position();
          } else if (parts.secondSemi == parts.headerEnd) {
            parts.secondSemi = cursor.position();
          }
          parts.kind = ast::SyntaxKind::ForStmt;
        } else if (kind == ast::SyntaxKind::InKeyword && parts.firstSemi == parts.headerEnd &&
                   parts.inIndex == parts.headerEnd) {
          parts.inIndex = cursor.position();
          parts.kind = ast::SyntaxKind::ForInStatement;
        }
      }

      if (kind == ast::SyntaxKind::LeftParen) {
        ++parenDepth;
      } else if (kind == ast::SyntaxKind::RightParen) {
        if (parenDepth > 0) { --parenDepth; }
      } else if (kind == ast::SyntaxKind::LeftBracket) {
        ++bracketDepth;
      } else if (kind == ast::SyntaxKind::RightBracket) {
        if (bracketDepth > 0) { --bracketDepth; }
      } else if (kind == ast::SyntaxKind::LeftBrace) {
        ++braceDepth;
      } else if (kind == ast::SyntaxKind::RightBrace) {
        if (braceDepth > 0) { --braceDepth; }
      }
      cursor.advance();
    }
    parts.bodyEnd = consumeStatementBodyEnd(parts.bodyStart, limit);
    parts.end = parts.bodyEnd;
    return parts;
  }

  SourceElementBoundary consumeSourceElement(TokenCursor& cursor, size_t limit) const {
    const size_t start = cursor.position();
    SourceElementBoundary boundary;
    boundary.start = start;
    boundary.nodeStart = start;
    boundary.head = start;
    boundary.end = limit;
    if (start >= limit) {
      cursor.moveTo(limit);
      return boundary;
    }

    const size_t nodeStart = skipOuterAttributePrefix(start, limit);
    boundary.nodeStart = nodeStart;
    cursor.moveTo(nodeStart);
    if (nodeStart >= limit) {
      boundary.head = nodeStart;
      boundary.end = limit;
      cursor.moveTo(limit);
      return boundary;
    }

    while (cursor.position() < limit && isDeclarationModifier(cursor.peek())) { cursor.advance(); }
    const size_t head = cursor.position();
    boundary.head = head;
    if (head >= limit) {
      boundary.end = limit;
      cursor.moveTo(limit);
      return boundary;
    }

    switch (cursor.peek()) {
      case ast::SyntaxKind::ModuleKeyword:
        boundary.kind = ast::SyntaxKind::ModuleDeclaration;
        boundary.end = consumeSimpleStatementEnd(head, limit);
        break;
      case ast::SyntaxKind::ImportKeyword:
        boundary.kind = ast::SyntaxKind::ImportDeclaration;
        boundary.end = consumeSimpleStatementEnd(head, limit);
        break;
      case ast::SyntaxKind::ExportKeyword:
        boundary.kind = ast::SyntaxKind::ExportDeclaration;
        boundary.end = consumeExportDeclarationEnd(head, limit);
        break;
      case ast::SyntaxKind::MutKeyword:
      case ast::SyntaxKind::LetKeyword:
      case ast::SyntaxKind::ConstKeyword:
        boundary.kind = ast::SyntaxKind::LetStmt;
        boundary.end = consumeBindingDeclarationEnd(head, limit);
        break;
      case ast::SyntaxKind::FunKeyword:
        boundary.kind = ast::SyntaxKind::FunctionDecl;
        boundary.end = consumeFunctionDeclarationEnd(head, limit);
        break;
      case ast::SyntaxKind::ClassKeyword:
        boundary.kind = ast::SyntaxKind::ClassDecl;
        boundary.end = consumeNamedTypeDeclarationEnd(head, limit);
        break;
      case ast::SyntaxKind::StructKeyword:
        boundary.kind = ast::SyntaxKind::StructDecl;
        boundary.end = consumeNamedTypeDeclarationEnd(head, limit);
        break;
      case ast::SyntaxKind::InterfaceKeyword:
        boundary.kind = ast::SyntaxKind::InterfaceDecl;
        boundary.end = consumeNamedTypeDeclarationEnd(head, limit);
        break;
      case ast::SyntaxKind::EnumKeyword:
        boundary.kind = ast::SyntaxKind::EnumDeclaration;
        boundary.end = consumeNamedTypeDeclarationEnd(head, limit);
        break;
      case ast::SyntaxKind::ErrorKeyword:
        boundary.kind = ast::SyntaxKind::ErrorDecl;
        boundary.end = consumeBracedDeclarationEnd(head, limit);
        break;
      case ast::SyntaxKind::TypeKeyword:
      case ast::SyntaxKind::AliasKeyword:
        boundary.kind = ast::SyntaxKind::AliasDecl;
        boundary.end = consumeSimpleStatementEnd(head, limit);
        break;
      case ast::SyntaxKind::IfKeyword:
        boundary.kind = ast::SyntaxKind::IfStmt;
        boundary.end = consumeIfStatementEnd(head, limit);
        break;
      case ast::SyntaxKind::MatchKeyword:
        boundary.kind = ast::SyntaxKind::MatchStmt;
        boundary.end = consumeMatchStatementEnd(head, limit);
        break;
      case ast::SyntaxKind::WhileKeyword:
        boundary.kind = ast::SyntaxKind::WhileStmt;
        boundary.end = consumeWhileStatementEnd(head, limit);
        break;
      case ast::SyntaxKind::DoKeyword:
        boundary.kind = ast::SyntaxKind::DoWhileStatement;
        boundary.end = consumeDoWhileStatementEnd(head, limit);
        break;
      case ast::SyntaxKind::ForKeyword: {
        const ForStatementParts parts = parseForStatementParts(head, limit);
        boundary.end = parts.end;
        boundary.kind = parts.kind;
        break;
      }
      case ast::SyntaxKind::BreakKeyword:
        boundary.kind = ast::SyntaxKind::BreakStmt;
        boundary.end = consumeSimpleStatementEnd(head, limit);
        break;
      case ast::SyntaxKind::ContinueKeyword:
        boundary.kind = ast::SyntaxKind::ContinueStatement;
        boundary.end = consumeSimpleStatementEnd(head, limit);
        break;
      case ast::SyntaxKind::ReturnKeyword:
        boundary.kind = ast::SyntaxKind::ReturnStmt;
        boundary.end = consumeSimpleStatementEnd(head, limit);
        break;
      case ast::SyntaxKind::DebuggerKeyword:
        boundary.kind = ast::SyntaxKind::DebuggerStatement;
        boundary.end = consumeSimpleStatementEnd(head, limit);
        break;
      case ast::SyntaxKind::SuspendKeyword:
        boundary.kind = ast::SyntaxKind::SuspendStatement;
        boundary.end = consumeSimpleStatementEnd(head, limit);
        break;
      case ast::SyntaxKind::SpawnKeyword:
        boundary.kind = ast::SyntaxKind::ExpressionStatement;
        boundary.end = consumeSpawnStatementEnd(head, limit);
        break;
      case ast::SyntaxKind::Semicolon:
        boundary.kind = ast::SyntaxKind::EmptyStatement;
        boundary.end = head + 1;
        break;
      case ast::SyntaxKind::LeftBrace:
        boundary.end = consumeBracedBodyEnd(head, limit);
        boundary.kind = !outerAttributePrefixContainsZomCfg(start, boundary.end) &&
                                looksLikeObjectLiteralExpression(head, boundary.end)
                            ? ast::SyntaxKind::ExpressionStatement
                            : ast::SyntaxKind::BlockStmt;
        break;
      case ast::SyntaxKind::Identifier:
        if (isSoftKeyword(head, "macro"_zc)) {
          boundary.kind = ast::SyntaxKind::MacroRulesDecl;
          boundary.end = consumeBracedDeclarationEnd(head, limit);
        } else if (isExternDeclarationStart(head, limit)) {
          boundary.kind = ast::SyntaxKind::ExternBlock;
          boundary.end = consumeExternDeclarationEnd(head, limit);
        } else if (isSoftKeyword(head, "impl"_zc) ||
                   (isSoftKeyword(head, "unsafe"_zc) && head + 1 < limit &&
                    isSoftKeyword(head + 1, "impl"_zc))) {
          boundary.kind = ast::SyntaxKind::StandaloneImplDecl;
          boundary.end = consumeBracedDeclarationEnd(head, limit);
        } else if (head + 1 < limit && kindAt(head + 1) == ast::SyntaxKind::Colon) {
          boundary.kind = ast::SyntaxKind::LabeledStatement;
          boundary.end = consumeLabeledStatementEnd(head, limit);
        } else {
          boundary.kind = ast::SyntaxKind::ExpressionStatement;
          boundary.end = consumeSimpleStatementEnd(head, limit);
        }
        break;
      default:
        boundary.kind = ast::SyntaxKind::ExpressionStatement;
        boundary.end = consumeSimpleStatementEnd(head, limit);
        break;
    }

    if (boundary.end <= start) { boundary.end = start + 1; }
    cursor.moveTo(boundary.end);
    return boundary;
  }

  SourceElementParseResult parseSourceElement(ast::TreeBuilder& builder, TokenCursor& cursor,
                                              size_t limit) const {
    SourceElementParseResult result;
    result.boundary = consumeSourceElement(cursor, limit);
    if (result.boundary.start >= limit || result.boundary.nodeStart >= limit ||
        result.boundary.head >= limit || result.boundary.end <= result.boundary.start) {
      result.boundary.end = limit;
      return result;
    }

    result.attrs = parseOuterAttributeList(builder, result.boundary.start, result.boundary.end);
    result.node = parseSourceElementOfKind(builder, result.boundary.nodeStart, result.boundary.end,
                                           result.boundary.kind);
    return result;
  }

  ast::NodeId parseBlock(ast::TreeBuilder& builder, size_t openBrace, size_t limit,
                         bool allowFinalExpression = false) const {
    zc::Vector<ast::NodeId> items;
    if (openBrace >= limit || kindAt(openBrace) != ast::SyntaxKind::LeftBrace) {
      ast::NodePayload payload;
      writeNodeList(payload, ast::kBlockStmtStmtsFirstWord, ast::kBlockStmtStmtsSizeWord,
                    builder.makeList(items.asPtr()));
      return builder.makeNode(ast::SyntaxKind::BlockStmt, rangeFor(openBrace, openBrace), payload);
    }

    const size_t closeBrace = findMatchingRightBrace(openBrace, limit);
    const size_t bodyEnd = closeBrace < limit ? closeBrace : limit;
    TokenCursor cursor = tokenCursorAt(openBrace + 1);
    while (cursor.position() < bodyEnd) {
      const size_t statementStart = cursor.position();
      SourceElementParseResult itemResult;
      itemResult.boundary = consumeSourceElement(cursor, bodyEnd);
      if (itemResult.boundary.start >= bodyEnd || itemResult.boundary.nodeStart >= bodyEnd ||
          itemResult.boundary.head >= bodyEnd ||
          itemResult.boundary.end <= itemResult.boundary.start) {
        itemResult.boundary.end = bodyEnd;
      } else {
        itemResult.attrs =
            parseOuterAttributeList(builder, itemResult.boundary.start, itemResult.boundary.end);
        const bool finalExpression =
            allowFinalExpression &&
            itemResult.boundary.kind == ast::SyntaxKind::ExpressionStatement &&
            itemResult.boundary.end >= bodyEnd &&
            (itemResult.boundary.end == 0 ||
             kindAt(itemResult.boundary.end - 1) != ast::SyntaxKind::Semicolon);
        itemResult.node =
            finalExpression
                ? parseExpressionStatementWithoutSemicolon(builder, itemResult.boundary.nodeStart,
                                                           itemResult.boundary.end)
                : parseSourceElementOfKind(builder, itemResult.boundary.nodeStart,
                                           itemResult.boundary.end, itemResult.boundary.kind);
      }
      const size_t statementEnd = itemResult.boundary.end;
      if (outerAttributePrefixContainsZomCfg(statementStart, statementEnd) &&
          itemResult.boundary.kind != ast::SyntaxKind::BlockStmt) {
        diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(
            tokenAt(effectiveStatementStart(statementStart, statementEnd)).getLocation(),
            "cfg-gated block"_zc);
      }
      if (itemResult.node) {
        items.add(makeStatementListItem(builder, itemResult.node,
                                        rangeFor(statementStart, statementEnd), itemResult.attrs));
      }
      if (cursor.position() <= statementStart) { cursor.moveTo(statementStart + 1); }
    }

    ast::NodePayload payload;
    writeNodeList(payload, ast::kBlockStmtStmtsFirstWord, ast::kBlockStmtStmtsSizeWord,
                  builder.makeList(items.asPtr()));
    return builder.makeNode(ast::SyntaxKind::BlockStmt, rangeFor(openBrace, bodyEnd + 1), payload);
  }

  ast::NodeId parseStatementBody(ast::TreeBuilder& builder, size_t start, size_t end) const {
    if (start >= end) { return parseBlock(builder, end, end); }

    if (kindAt(start) == ast::SyntaxKind::LeftBrace) {
      return parseBlock(builder, start, consumeBracedBodyEnd(start, end));
    }

    TokenCursor cursor = tokenCursorAt(start);
    const SourceElementBoundary boundary = consumeSourceElement(cursor, end);
    if (boundary.nodeStart >= end || boundary.head >= end || boundary.end <= start) {
      return ast::NodeId();
    }
    return parseSourceElementOfKind(builder, boundary.nodeStart, boundary.end, boundary.kind);
  }

  ast::NodeId parseModuleDeclaration(ast::TreeBuilder& builder, size_t start, size_t end) const {
    size_t pathEnd = end;
    const size_t semicolon = findTopLevelToken(start + 1, end, ast::SyntaxKind::Semicolon);
    const size_t equals = findTopLevelToken(start + 1, end, ast::SyntaxKind::Equals);
    const size_t bodyOpen = findTopLevelToken(start + 1, end, ast::SyntaxKind::LeftBrace);
    if (semicolon < pathEnd) { pathEnd = semicolon; }
    if (equals < pathEnd) { pathEnd = equals; }
    if (bodyOpen < pathEnd) { pathEnd = bodyOpen; }

    const bool simpleModuleName = semicolon < end && equals >= semicolon && bodyOpen >= end;
    if (simpleModuleName) {
      const size_t colonColon = findTopLevelToken(start + 1, pathEnd, ast::SyntaxKind::ColonColon);
      if (colonColon < pathEnd) {
        diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
            tokenAt(colonColon).getLocation());
        return ast::NodeId();
      }
    }

    ast::NodePayload payload;
    writeNode(payload, ast::kModuleDeclarationPathWord,
              makeModulePath(builder, start + 1, pathEnd));
    return builder.makeNode(ast::SyntaxKind::ModuleDeclaration, rangeFor(start, end), payload);
  }

  ast::NodeId parseImportDeclaration(ast::TreeBuilder& builder, size_t start, size_t end) const {
    const size_t clauseStart = start + 1;
    const size_t clauseEnd =
        end > clauseStart && kindAt(end - 1) == ast::SyntaxKind::Semicolon ? end - 1 : end;
    const size_t pathEnd = findModulePathEnd(clauseStart, clauseEnd);
    const size_t groupOpen = findModuleSpecifierGroupOpen(pathEnd, clauseEnd);
    zc::Vector<ast::NodeId> specifiers;
    ast::NodePayload payload;
    if (pathEnd <= clauseStart) {
      diagnosticEngine.diagnose<diagnostics::DiagID::IdentifierExpected>(
          diagnosticLoc(clauseStart));
    }
    diagnoseImportPathSyntax(clauseStart, clauseEnd, pathEnd, groupOpen);
    writeNode(payload, ast::kImportDeclarationPathWord,
              makeModulePath(builder, clauseStart, pathEnd));
    if (groupOpen < clauseEnd) {
      const size_t closeBrace = findMatchingRightBrace(groupOpen, clauseEnd);
      specifiers = parseImportSpecifierList(builder, groupOpen + 1,
                                            closeBrace < clauseEnd ? closeBrace : clauseEnd);
    } else if (pathEnd < clauseEnd && kindAt(pathEnd) == ast::SyntaxKind::AsKeyword) {
      const size_t aliasIndex = pathEnd + 1;
      if (aliasIndex < clauseEnd && kindAt(aliasIndex) == ast::SyntaxKind::Identifier) {
        writeIdent(payload, ast::kImportDeclarationAliasWord, internIdent(builder, aliasIndex));
      } else {
        diagnosticEngine.diagnose<diagnostics::DiagID::IdentifierExpected>(
            diagnosticLoc(aliasIndex));
      }
    }
    writeNodeList(payload, ast::kImportDeclarationSpecifiersFirstWord,
                  ast::kImportDeclarationSpecifiersSizeWord, builder.makeList(specifiers.asPtr()));
    return builder.makeNode(ast::SyntaxKind::ImportDeclaration, rangeFor(start, end), payload);
  }

  ast::NodeId parseExportDeclaration(ast::TreeBuilder& builder, size_t start, size_t end) const {
    const size_t clauseStart = start + 1;
    const size_t clauseEnd =
        end > clauseStart && kindAt(end - 1) == ast::SyntaxKind::Semicolon ? end - 1 : end;
    zc::Vector<ast::NodeId> specifiers;
    ast::NodePayload payload;
    size_t declarationHead = clauseStart;
    while (declarationHead < clauseEnd && isDeclarationModifier(kindAt(declarationHead))) {
      ++declarationHead;
    }
    if (declarationHead < clauseEnd && (isDeclarationHead(kindAt(declarationHead)) ||
                                        isSoftDeclarationHead(declarationHead, clauseEnd))) {
      TokenCursor cursor = tokenCursorAt(clauseStart);
      const SourceElementParseResult declaration = parseSourceElement(builder, cursor, end);
      writeNode(payload, ast::kExportDeclarationDeclarationWord, declaration.node);
    } else if (clauseStart < clauseEnd && kindAt(clauseStart) == ast::SyntaxKind::LeftBrace) {
      const size_t closeBrace = findMatchingRightBrace(clauseStart, clauseEnd);
      specifiers = parseExportSpecifierList(builder, clauseStart + 1,
                                            closeBrace < clauseEnd ? closeBrace : clauseEnd);
    } else if (clauseStart < clauseEnd) {
      const size_t pathEnd = findModulePathEnd(clauseStart, clauseEnd);
      const size_t groupOpen = findModuleSpecifierGroupOpen(pathEnd, clauseEnd);
      if (pathEnd <= clauseStart) {
        diagnosticEngine.diagnose<diagnostics::DiagID::IdentifierExpected>(
            diagnosticLoc(clauseStart));
      } else if (groupOpen < clauseEnd) {
        writeNode(payload, ast::kExportDeclarationPathWord,
                  makeModulePath(builder, clauseStart, pathEnd));
        const size_t closeBrace = findMatchingRightBrace(groupOpen, clauseEnd);
        specifiers = parseExportSpecifierList(builder, groupOpen + 1,
                                              closeBrace < clauseEnd ? closeBrace : clauseEnd);
      } else if (kindAt(clauseStart) != ast::SyntaxKind::DefaultKeyword) {
        diagnosticEngine.diagnose<diagnostics::DiagID::DeclarationExpected>(
            diagnosticLoc(clauseStart));
      }
    }
    writeNodeList(payload, ast::kExportDeclarationSpecifiersFirstWord,
                  ast::kExportDeclarationSpecifiersSizeWord, builder.makeList(specifiers.asPtr()));
    return builder.makeNode(ast::SyntaxKind::ExportDeclaration, rangeFor(start, end), payload);
  }

  struct VariableDeclaratorParseResult {
    ast::NodeId node;
    size_t next = 0;
  };

  size_t consumeVariableDeclaratorPattern(TokenCursor& cursor, size_t limit) const {
    int32_t parenDepth = 0;
    int32_t bracketDepth = 0;
    int32_t braceDepth = 0;

    while (cursor.position() < limit) {
      const ast::SyntaxKind kind = cursor.peek();
      if (parenDepth == 0 && bracketDepth == 0 && braceDepth == 0) {
        if (kind == ast::SyntaxKind::Colon || kind == ast::SyntaxKind::Equals ||
            kind == ast::SyntaxKind::Comma) {
          return cursor.position();
        }
      }

      if (kind == ast::SyntaxKind::LeftParen) {
        ++parenDepth;
      } else if (kind == ast::SyntaxKind::RightParen) {
        if (parenDepth > 0) { --parenDepth; }
      } else if (kind == ast::SyntaxKind::LeftBracket) {
        ++bracketDepth;
      } else if (kind == ast::SyntaxKind::RightBracket) {
        if (bracketDepth > 0) { --bracketDepth; }
      } else if (kind == ast::SyntaxKind::LeftBrace) {
        ++braceDepth;
      } else if (kind == ast::SyntaxKind::RightBrace) {
        if (braceDepth > 0) { --braceDepth; }
      }
      cursor.advance();
    }

    return cursor.position();
  }

  bool isInitializerGenericAngle(size_t openAngle, size_t limit) const {
    const size_t closeAngle = findMatchingAngleClose(openAngle, limit);
    if (closeAngle >= limit || closeAngle + 1 >= limit) { return false; }

    const ast::SyntaxKind next = kindAt(closeAngle + 1);
    return next == ast::SyntaxKind::LeftParen || next == ast::SyntaxKind::LeftBrace;
  }

  size_t consumeVariableInitializer(TokenCursor& cursor, size_t limit) const {
    while (cursor.position() < limit) {
      const ast::SyntaxKind kind = cursor.peek();
      if (kind == ast::SyntaxKind::Comma) { return cursor.position(); }

      if (kind == ast::SyntaxKind::LeftParen) {
        const size_t closeParen = findMatchingRightParen(cursor.position(), limit);
        cursor.moveTo(closeParen < limit ? closeParen + 1 : limit);
        continue;
      }
      if (kind == ast::SyntaxKind::LeftBracket) {
        const size_t closeBracket = findMatchingRightBracket(cursor.position(), limit);
        cursor.moveTo(closeBracket < limit ? closeBracket + 1 : limit);
        continue;
      }
      if (kind == ast::SyntaxKind::LeftBrace) {
        const size_t closeBrace = findMatchingRightBrace(cursor.position(), limit);
        cursor.moveTo(closeBrace < limit ? closeBrace + 1 : limit);
        continue;
      }
      if (kind == ast::SyntaxKind::LessThan &&
          isInitializerGenericAngle(cursor.position(), limit)) {
        const size_t closeAngle = findMatchingAngleClose(cursor.position(), limit);
        cursor.moveTo(closeAngle + 1);
        continue;
      }

      cursor.advance();
    }

    return cursor.position();
  }

  VariableDeclaratorParseResult parseVariableDeclarator(ast::TreeBuilder& builder,
                                                        TokenCursor& cursor, size_t limit) const {
    const size_t start = cursor.position();
    if (start >= limit) {
      diagnosticEngine.diagnose<diagnostics::DiagID::VariableDeclarationExpected>(
          diagnosticLoc(start));
      return VariableDeclaratorParseResult();
    }

    const size_t patternEnd = consumeVariableDeclaratorPattern(cursor, limit);
    if (patternEnd <= start) {
      diagnosticEngine.diagnose<diagnostics::DiagID::VariableDeclarationExpected>(
          diagnosticLoc(start));
      return VariableDeclaratorParseResult();
    }

    const size_t errorCountBeforePattern = diagnosticEngine.errorCount();
    const ast::NodeId pattern = parsePatternRange(builder, start, patternEnd);
    if (!pattern) {
      if (diagnosticEngine.errorCount() == errorCountBeforePattern) {
        diagnosticEngine.diagnose<diagnostics::DiagID::VariableDeclarationExpected>(
            diagnosticLoc(start));
      }
      return VariableDeclaratorParseResult();
    }

    ast::NodePayload payload;
    writeNode(payload, ast::kVariableDeclaratorPatternWord, pattern);

    if (cursor.position() < limit && cursor.peek() == ast::SyntaxKind::Colon) {
      cursor.advance();
      const size_t typeStart = cursor.position();
      TypeParseResult ty = parseTypeExpression(builder, cursor, limit);
      if (!ty.node) {
        diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(typeStart));
        return VariableDeclaratorParseResult();
      }
      writeNode(payload, ast::kVariableDeclaratorTyWord, ty.node);
    }

    if (cursor.position() < limit && cursor.peek() == ast::SyntaxKind::Equals) {
      cursor.advance();
      const size_t initStart = cursor.position();
      const size_t initEnd = consumeVariableInitializer(cursor, limit);
      if (initStart >= initEnd) {
        diagnoseExpressionExpected(initStart);
        return VariableDeclaratorParseResult();
      }

      const ast::NodeId init = parseRequiredExpression(builder, initStart, initEnd);
      if (!init) { return VariableDeclaratorParseResult(); }
      writeNode(payload, ast::kVariableDeclaratorInitWord, init);
    }

    return {builder.makeNode(ast::SyntaxKind::VariableDeclarator,
                             rangeFor(start, cursor.position()), payload),
            cursor.position()};
  }

  ast::NodeId parseVariableDeclaratorList(ast::TreeBuilder& builder, size_t start,
                                          size_t end) const {
    zc::Vector<ast::NodeId> declarators;
    TokenCursor cursor = tokenCursorAt(start);
    while (cursor.position() < end) {
      if (cursor.peek() == ast::SyntaxKind::Comma) {
        if (declarators.size() > 0 && cursor.position() + 1 >= end) {
          cursor.advance();
          break;
        }
        diagnosticEngine.diagnose<diagnostics::DiagID::VariableDeclarationExpected>(
            diagnosticLoc(cursor.position()));
        return ast::NodeId();
      }

      const size_t declaratorStart = cursor.position();
      const VariableDeclaratorParseResult declarator =
          parseVariableDeclarator(builder, cursor, end);
      if (!declarator.node) { return ast::NodeId(); }
      declarators.add(declarator.node);

      if (cursor.position() <= declaratorStart) { cursor.moveTo(declaratorStart + 1); }
      if (cursor.position() >= end) { break; }
      if (cursor.peek() == ast::SyntaxKind::Comma) {
        cursor.advance();
        continue;
      }

      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(
          diagnosticLoc(cursor.position()), ","_zc);
      return ast::NodeId();
    }

    if (declarators.size() == 0) {
      diagnosticEngine.diagnose<diagnostics::DiagID::VariableDeclarationExpected>(
          diagnosticLoc(start));
      return ast::NodeId();
    }

    ast::NodePayload payload;
    payload.words[ast::kVariableDeclaratorListNDeclsWord] =
        static_cast<uint32_t>(declarators.size());
    writeNodeList(payload, ast::kVariableDeclaratorListDeclsFirstWord,
                  ast::kVariableDeclaratorListDeclsSizeWord, builder.makeList(declarators.asPtr()));
    return builder.makeNode(ast::SyntaxKind::VariableDeclaratorList, rangeFor(start, end), payload);
  }

  ast::NodeId parseLetStatement(ast::TreeBuilder& builder, size_t start, size_t end) const {
    if (!requireTrailingSemicolon(start, end)) { return ast::NodeId(); }

    size_t declarationsEnd = end;
    while (start + 1 < declarationsEnd &&
           kindAt(declarationsEnd - 1) == ast::SyntaxKind::Semicolon) {
      --declarationsEnd;
    }

    const ast::NodeId declarations =
        parseVariableDeclaratorList(builder, start + 1, declarationsEnd);
    if (!declarations) { return ast::NodeId(); }

    ast::NodePayload payload;
    payload.words[ast::kLetStmtKindWord] = bindingDeclarationKindCode(kindAt(start));
    writeNode(payload, ast::kLetStmtDeclarationsWord, declarations);
    return builder.makeNode(ast::SyntaxKind::LetStmt, rangeFor(start, end), payload);
  }

  ast::NodeId parseReturnStatement(ast::TreeBuilder& builder, size_t start, size_t end) const {
    if (!requireTrailingSemicolon(start, end)) { return ast::NodeId(); }

    ast::NodePayload payload;
    size_t valueEnd = end;
    while (start + 1 < valueEnd && kindAt(valueEnd - 1) == ast::SyntaxKind::Semicolon) {
      --valueEnd;
    }
    if (start + 1 < valueEnd) {
      writeNode(payload, ast::kReturnStmtValueWord,
                parseRequiredExpression(builder, start + 1, valueEnd));
    }
    return builder.makeNode(ast::SyntaxKind::ReturnStmt, rangeFor(start, end), payload);
  }

  ast::NodeId parseSuspendStatement(ast::TreeBuilder& builder, size_t start, size_t end) const {
    if (!requireTrailingSemicolon(start, end)) { return ast::NodeId(); }

    ast::NodePayload payload;
    payload.words[ast::kSuspendStatementModeWord] = static_cast<uint32_t>(ast::SuspendMode::Bare);

    size_t cursor = start + 1;
    size_t valueEnd = end;
    if (cursor < valueEnd && kindAt(valueEnd - 1) == ast::SyntaxKind::Semicolon) { --valueEnd; }
    if (cursor >= valueEnd) {
      return builder.makeNode(ast::SyntaxKind::SuspendStatement, rangeFor(start, end), payload);
    }

    zc::StringPtr text = tokenAt(cursor).getValue();
    if (text.size() == 0) { text = tokenLabel(tokenAt(cursor)); }
    if (kindAt(cursor) == ast::SyntaxKind::Identifier && text == "until"_zc) {
      payload.words[ast::kSuspendStatementModeWord] =
          static_cast<uint32_t>(ast::SuspendMode::Until);
      writeNode(payload, ast::kSuspendStatementUntilCondWord,
                parseRequiredExpression(builder, cursor + 1, valueEnd));
      return builder.makeNode(ast::SyntaxKind::SuspendStatement, rangeFor(start, end), payload);
    }

    diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
        tokenAt(cursor).getLocation());
    return ast::NodeId();
  }

  size_t findMatchingRightParen(size_t openParen, size_t limit) const {
    if (openParen >= limit || kindAt(openParen) != ast::SyntaxKind::LeftParen) { return limit; }

    int32_t depth = 0;
    for (size_t index = openParen; index < limit; ++index) {
      if (kindAt(index) == ast::SyntaxKind::LeftParen) { ++depth; }
      if (kindAt(index) == ast::SyntaxKind::RightParen) {
        --depth;
        if (depth == 0) { return index; }
      }
    }
    return limit;
  }

  void conditionRangeAfterKeyword(size_t start, size_t end, size_t& condStart, size_t& condEnd,
                                  size_t& bodyStart) const {
    condStart = start + 1;
    condEnd = end;
    bodyStart = end;

    if (condStart < end && kindAt(condStart) == ast::SyntaxKind::LeftParen) {
      const size_t closeParen = findMatchingRightParen(condStart, end);
      condEnd = closeParen < end ? closeParen : end;
      bodyStart = closeParen < end ? closeParen + 1 : end;
      ++condStart;
      return;
    }

    const size_t brace = findTopLevelToken(condStart, end, ast::SyntaxKind::LeftBrace);
    condEnd = brace < end ? brace : end;
    bodyStart = brace < end ? brace : end;
  }

  ast::NodeId parseIfStatement(ast::TreeBuilder& builder, size_t start, size_t end) const {
    if (start + 1 >= end || kindAt(start + 1) != ast::SyntaxKind::LeftParen) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(start + 1),
                                                                    "("_zc);
      return ast::NodeId();
    }

    const IfStatementParts parts = parseIfStatementParts(start, end);

    ast::NodePayload payload;
    writeNode(payload, ast::kIfStmtCondWord,
              parseExpressionRange(builder, parts.condStart, parts.condEnd));
    writeNode(payload, ast::kIfStmtThenStmtWord,
              parseStatementBody(builder, parts.thenStart, parts.thenEnd));
    if (parts.elseIndex < end) {
      writeNode(payload, ast::kIfStmtElseStmtWord,
                parseStatementBody(builder, parts.elseStart, end));
    }
    return builder.makeNode(ast::SyntaxKind::IfStmt, rangeFor(start, end), payload);
  }

  ast::NodeId parseWhileStatement(ast::TreeBuilder& builder, size_t start, size_t end) const {
    if (start + 1 >= end || kindAt(start + 1) != ast::SyntaxKind::LeftParen) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(start + 1),
                                                                    "("_zc);
      return ast::NodeId();
    }

    const WhileStatementParts parts = parseWhileStatementParts(start, end);

    ast::NodePayload payload;
    writeNode(payload, ast::kWhileStmtCondWord,
              parseExpressionRange(builder, parts.condStart, parts.condEnd));
    writeNode(payload, ast::kWhileStmtBodyWord,
              parseStatementBody(builder, parts.bodyStart, parts.bodyEnd));
    return builder.makeNode(ast::SyntaxKind::WhileStmt, rangeFor(start, end), payload);
  }

  ast::NodeId parseDoWhileStatement(ast::TreeBuilder& builder, size_t start, size_t end) const {
    const DoWhileStatementParts parts = parseDoWhileStatementParts(start, end);
    ast::NodePayload payload;
    writeNode(payload, ast::kDoWhileStatementBodyWord,
              parseStatementBody(builder, parts.bodyStart, parts.bodyEnd));

    if (parts.whileIndex < end) {
      writeNode(payload, ast::kDoWhileStatementCondWord,
                parseExpressionRange(builder, parts.condStart, parts.condEnd));
    }
    return builder.makeNode(ast::SyntaxKind::DoWhileStatement, rangeFor(start, end), payload);
  }

  ast::NodeId parseBreakStatement(ast::TreeBuilder& builder, size_t start, size_t end) const {
    if (!requireTrailingSemicolon(start, end)) { return ast::NodeId(); }

    ast::NodePayload payload;
    if (start + 1 < end && kindAt(start + 1) == ast::SyntaxKind::Identifier) {
      writeIdent(payload, ast::kBreakStmtLabelWord, internIdent(builder, start + 1));
    }
    return builder.makeNode(ast::SyntaxKind::BreakStmt, rangeFor(start, end), payload);
  }

  ast::NodeId parseContinueStatement(ast::TreeBuilder& builder, size_t start, size_t end) const {
    if (!requireTrailingSemicolon(start, end)) { return ast::NodeId(); }

    ast::NodePayload payload;
    if (start + 1 < end && kindAt(start + 1) == ast::SyntaxKind::Identifier) {
      writeIdent(payload, ast::kContinueStatementLabelWord, internIdent(builder, start + 1));
    }
    return builder.makeNode(ast::SyntaxKind::ContinueStatement, rangeFor(start, end), payload);
  }

  ast::NodeId parseLabeledStatement(ast::TreeBuilder& builder, size_t start, size_t end) const {
    if (isOuterAttributeStart(start + 2, end)) {
      diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
          tokenAt(start + 2).getLocation());
      return ast::NodeId();
    }

    ast::NodePayload payload;
    writeIdent(payload, ast::kLabeledStatementLabelWord, internIdent(builder, start));
    if (start + 2 < end) {
      writeNode(payload, ast::kLabeledStatementStatementWord,
                parseStatementBody(builder, start + 2, end));
    }
    return builder.makeNode(ast::SyntaxKind::LabeledStatement, rangeFor(start, end), payload);
  }

  ast::NodeId parseForStatement(ast::TreeBuilder& builder, size_t start, size_t end) const {
    if (start + 1 >= end || kindAt(start + 1) != ast::SyntaxKind::LeftParen) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(start + 1),
                                                                    "("_zc);
      return ast::NodeId();
    }

    const ForStatementParts parts = parseForStatementParts(start, end);
    if (parts.firstSemi >= parts.headerEnd || parts.secondSemi >= parts.headerEnd) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(parts.headerEnd),
                                                                    ";"_zc);
      return ast::NodeId();
    }

    ast::NodePayload payload;
    if (parts.headerStart < parts.firstSemi) {
      TokenCursor cursor = tokenCursorAt(parts.headerStart);
      const size_t initEnd =
          parts.firstSemi < parts.headerEnd ? parts.firstSemi + 1 : parts.firstSemi;
      const SourceElementParseResult init = parseSourceElement(builder, cursor, initEnd);
      writeNode(payload, ast::kForStmtInitWord, init.node);
    }
    if (parts.firstSemi < parts.headerEnd && parts.firstSemi + 1 < parts.secondSemi) {
      writeNode(payload, ast::kForStmtCondWord,
                parseExpressionRange(builder, parts.firstSemi + 1, parts.secondSemi));
    }
    if (parts.secondSemi < parts.headerEnd && parts.secondSemi + 1 < parts.headerEnd) {
      writeNode(payload, ast::kForStmtUpdateWord,
                parseExpressionRange(builder, parts.secondSemi + 1, parts.headerEnd));
    }
    writeNode(payload, ast::kForStmtBodyWord,
              parseStatementBody(builder, parts.bodyStart, parts.bodyEnd));
    return builder.makeNode(ast::SyntaxKind::ForStmt, rangeFor(start, end), payload);
  }

  ast::NodeId parseForInStatement(ast::TreeBuilder& builder, size_t start, size_t end) const {
    const ForStatementParts parts = parseForStatementParts(start, end);

    ast::NodePayload payload;
    if (parts.headerStart < parts.inIndex) {
      size_t bindingStart = parts.headerStart;
      if (kindAt(bindingStart) == ast::SyntaxKind::LetKeyword ||
          kindAt(bindingStart) == ast::SyntaxKind::MutKeyword) {
        ++bindingStart;
      }
      writeNode(payload, ast::kForInStatementBindingWord,
                parsePatternRange(builder, bindingStart, parts.inIndex));
    }
    if (parts.inIndex < parts.headerEnd && parts.inIndex + 1 < parts.headerEnd) {
      writeNode(payload, ast::kForInStatementExpressionWord,
                parseExpressionRange(builder, parts.inIndex + 1, parts.headerEnd));
    }
    writeNode(payload, ast::kForInStatementBodyWord,
              parseStatementBody(builder, parts.bodyStart, parts.bodyEnd));
    return builder.makeNode(ast::SyntaxKind::ForInStatement, rangeFor(start, end), payload);
  }

  ast::NodeId parseMatchStatement(ast::TreeBuilder& builder, size_t start, size_t end) const {
    const MatchStatementParts parts = parseMatchStatementParts(start, end);

    zc::Vector<ast::NodeId> arms;
    if (parts.bodyOpen < end && kindAt(parts.bodyOpen) == ast::SyntaxKind::LeftBrace) {
      const size_t bodyEnd = parts.bodyClose;
      size_t cursor = parts.bodyOpen + 1;
      while (cursor < bodyEnd) {
        if (kindAt(cursor) == ast::SyntaxKind::WhenKeyword) {
          const size_t arrow =
              findTopLevelToken(cursor + 1, bodyEnd, ast::SyntaxKind::EqualsGreaterThan);
          if (arrow >= bodyEnd) {
            diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(cursor + 1),
                                                                          "=>"_zc);
            return ast::NodeId();
          }

          const size_t guard = findTopLevelToken(cursor + 1, arrow, ast::SyntaxKind::IfKeyword);
          const size_t patternEnd = guard < arrow ? guard : arrow;
          const size_t statementStart = arrow + 1;
          const size_t statementEnd = consumeStatementBodyEnd(statementStart, bodyEnd);

          ast::NodePayload armPayload;
          writeNode(armPayload, ast::kMatchArmStmtPatternWord,
                    parsePatternRange(builder, cursor + 1, patternEnd));
          if (guard < arrow) {
            writeNode(armPayload, ast::kMatchArmStmtGuardWord,
                      parseRequiredExpression(builder, guard + 1, arrow));
          }
          writeNode(armPayload, ast::kMatchArmStmtBodyWord,
                    parseStatementBody(builder, statementStart, statementEnd));
          arms.add(builder.makeNode(ast::SyntaxKind::MatchArmStmt, rangeFor(cursor, statementEnd),
                                    armPayload));
          cursor = statementEnd > cursor ? statementEnd : cursor + 1;
          continue;
        }

        if (kindAt(cursor) == ast::SyntaxKind::DefaultKeyword) {
          const size_t arrow =
              findTopLevelToken(cursor + 1, bodyEnd, ast::SyntaxKind::EqualsGreaterThan);
          if (arrow >= bodyEnd) {
            diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(cursor + 1),
                                                                          "=>"_zc);
            return ast::NodeId();
          }

          const size_t statementStart = arrow + 1;
          const size_t statementEnd = consumeStatementBodyEnd(statementStart, bodyEnd);

          ast::NodePayload patternPayload;
          ast::NodePayload armPayload;
          writeNode(armPayload, ast::kMatchArmStmtPatternWord,
                    builder.makeNode(ast::SyntaxKind::WildcardPattern, rangeFor(cursor, cursor + 1),
                                     patternPayload));
          writeNode(armPayload, ast::kMatchArmStmtBodyWord,
                    parseStatementBody(builder, statementStart, statementEnd));
          arms.add(builder.makeNode(ast::SyntaxKind::MatchArmStmt, rangeFor(cursor, statementEnd),
                                    armPayload));
          cursor = statementEnd > cursor ? statementEnd : cursor + 1;
          continue;
        }

        diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(tokenAt(cursor).getLocation(),
                                                                      "when"_zc);
        return ast::NodeId();
      }
    }

    ast::NodePayload payload;
    writeNode(payload, ast::kMatchStmtScrutineeWord,
              parseExpressionRange(builder, parts.scrutineeStart, parts.scrutineeEnd));
    writeNodeList(payload, ast::kMatchStmtArmsFirstWord, ast::kMatchStmtArmsSizeWord,
                  builder.makeList(arms.asPtr()));
    return builder.makeNode(ast::SyntaxKind::MatchStmt, rangeFor(start, end), payload);
  }

  ast::NodeId parseExternFunctionDecl(ast::TreeBuilder& builder, size_t start, size_t end,
                                      uint32_t abi) const {
    if (end <= start || kindAt(end - 1) != ast::SyntaxKind::Semicolon) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(end), ";"_zc);
      return ast::NodeId();
    }

    const FunctionDeclarationParts parts = parseFunctionDeclarationParts(start, end);
    if (parts.nameIndex >= end || parts.openParen >= end || parts.closeParen >= end) {
      diagnosticEngine.diagnose<diagnostics::DiagID::IdentifierExpected>(diagnosticLoc(start + 1));
      return ast::NodeId();
    }

    ast::NodePayload payload;
    writeIdent(payload, ast::kExternDeclNameWord, internIdent(builder, parts.nameIndex));
    payload.words[ast::kExternDeclAbiWord] = abi;
    writeNodeList(payload, ast::kExternDeclParamsFirstWord, ast::kExternDeclParamsSizeWord,
                  parseFunctionParameterNodeList(builder, parts.openParen, parts.closeParen));
    if (parts.arrow < parts.headerEnd) {
      const size_t retEnd = parts.raises < parts.headerEnd ? parts.raises : parts.headerEnd;
      writeNode(payload, ast::kExternDeclRetTyWord,
                parseTypeRange(builder, parts.arrow + 1, retEnd));
    }
    if (parts.raises < parts.headerEnd) {
      writeNode(payload, ast::kExternDeclRaisesTyWord,
                parseTypeRange(builder, parts.raises + 1, parts.headerEnd));
    }
    return builder.makeNode(ast::SyntaxKind::ExternDecl, rangeFor(start, end), payload);
  }

  ast::NodeId parseExternVarDecl(ast::TreeBuilder& builder, size_t start, size_t end,
                                 uint32_t abi) const {
    const size_t nameIndex = start + 1;
    const size_t colon = findTopLevelToken(nameIndex + 1, end, ast::SyntaxKind::Colon);
    if (nameIndex >= end || kindAt(nameIndex) != ast::SyntaxKind::Identifier) {
      diagnosticEngine.diagnose<diagnostics::DiagID::IdentifierExpected>(diagnosticLoc(nameIndex));
      return ast::NodeId();
    }
    if (colon >= end) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(nameIndex + 1),
                                                                    ":"_zc);
      return ast::NodeId();
    }

    size_t typeEnd = end;
    if (typeEnd > colon && kindAt(typeEnd - 1) == ast::SyntaxKind::Semicolon) { --typeEnd; }
    ast::NodePayload payload;
    writeIdent(payload, ast::kExternVarDeclNameWord, internIdent(builder, nameIndex));
    writeNode(payload, ast::kExternVarDeclTyWord, parseTypeRange(builder, colon + 1, typeEnd));
    payload.words[ast::kExternVarDeclAbiWord] = abi;
    payload.words[ast::kExternVarDeclIsMutWord] = 1;
    return builder.makeNode(ast::SyntaxKind::ExternVarDecl, rangeFor(start, end), payload);
  }

  ast::NodeId parseExternTypeAliasDecl(ast::TreeBuilder& builder, size_t start, size_t end) const {
    const size_t nameIndex = start + 1;
    const size_t equals = findTopLevelToken(nameIndex + 1, end, ast::SyntaxKind::Equals);
    if (nameIndex >= end || kindAt(nameIndex) != ast::SyntaxKind::Identifier) {
      diagnosticEngine.diagnose<diagnostics::DiagID::IdentifierExpected>(diagnosticLoc(nameIndex));
      return ast::NodeId();
    }
    if (equals >= end) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(nameIndex + 1),
                                                                    "="_zc);
      return ast::NodeId();
    }

    size_t targetStart = equals + 1;
    if (targetStart < end && isSoftKeyword(targetStart, "opaque"_zc)) { ++targetStart; }
    size_t targetEnd = end;
    if (targetEnd > targetStart && kindAt(targetEnd - 1) == ast::SyntaxKind::Semicolon) {
      --targetEnd;
    }

    ast::NodePayload payload;
    writeIdent(payload, ast::kAliasDeclNameWord, internIdent(builder, nameIndex));
    writeNode(payload, ast::kAliasDeclTargetWord, parseTypeRange(builder, targetStart, targetEnd));
    return builder.makeNode(ast::SyntaxKind::AliasDecl, rangeFor(start, end), payload);
  }

  ast::NodeId parseExternBlockDeclaration(ast::TreeBuilder& builder, size_t start,
                                          size_t end) const {
    size_t cursor = start;
    if (isSoftKeyword(cursor, "unsafe"_zc)) { ++cursor; }
    if (cursor >= end || !isSoftKeyword(cursor, "extern"_zc)) {
      diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
          diagnosticLoc(cursor));
      return ast::NodeId();
    }
    ++cursor;

    uint32_t abi = static_cast<uint32_t>(ast::Abi::Cdecl);
    if (cursor < end && kindAt(cursor) == ast::SyntaxKind::StringLiteral) {
      if (!parseExternAbi(cursor, abi)) { return ast::NodeId(); }
      ++cursor;
    }

    if (cursor >= end || kindAt(cursor) != ast::SyntaxKind::LeftBrace) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(cursor), "{"_zc);
      return ast::NodeId();
    }

    const size_t bodyOpen = cursor;
    const size_t bodyClose = findMatchingRightBrace(bodyOpen, end);
    const size_t bodyEnd = bodyClose < end ? bodyClose : end;
    zc::Vector<ast::NodeId> items;
    cursor = bodyOpen + 1;
    while (cursor < bodyEnd) {
      if (kindAt(cursor) == ast::SyntaxKind::Semicolon) {
        ++cursor;
        continue;
      }

      const size_t itemStart = cursor;
      if (kindAt(itemStart) == ast::SyntaxKind::FunKeyword) {
        const size_t itemEnd = consumeFunctionDeclarationEnd(itemStart, bodyEnd);
        addNodeIfPresent(items, parseExternFunctionDecl(builder, itemStart, itemEnd, abi));
        cursor = itemEnd > itemStart ? itemEnd : itemStart + 1;
        continue;
      }
      if (isSoftKeyword(itemStart, "variable"_zc)) {
        const size_t itemEnd = consumeSimpleStatementEnd(itemStart, bodyEnd);
        addNodeIfPresent(items, parseExternVarDecl(builder, itemStart, itemEnd, abi));
        cursor = itemEnd > itemStart ? itemEnd : itemStart + 1;
        continue;
      }
      if (kindAt(itemStart) == ast::SyntaxKind::TypeKeyword) {
        const size_t itemEnd = consumeSimpleStatementEnd(itemStart, bodyEnd);
        addNodeIfPresent(items, parseExternTypeAliasDecl(builder, itemStart, itemEnd));
        cursor = itemEnd > itemStart ? itemEnd : itemStart + 1;
        continue;
      }

      diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
          tokenAt(itemStart).getLocation());
      ++cursor;
    }

    ast::NodePayload payload;
    payload.words[ast::kExternBlockAbiWord] = abi;
    writeNodeList(payload, ast::kExternBlockItemsFirstWord, ast::kExternBlockItemsSizeWord,
                  builder.makeList(items.asPtr()));
    return builder.makeNode(ast::SyntaxKind::ExternBlock, rangeFor(start, end), payload);
  }

  ast::NodeId parseMacroRulesDeclaration(ast::TreeBuilder& builder, size_t start,
                                         size_t end) const {
    if (!isSoftKeyword(start, "macro"_zc)) {
      diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(diagnosticLoc(start));
      return ast::NodeId();
    }

    const size_t nameIndex = start + 1;
    const size_t bangIndex = nameIndex + 1;
    if (nameIndex >= end || kindAt(nameIndex) != ast::SyntaxKind::Identifier) {
      diagnosticEngine.diagnose<diagnostics::DiagID::IdentifierExpected>(diagnosticLoc(nameIndex));
      return ast::NodeId();
    }
    if (bangIndex >= end || kindAt(bangIndex) != ast::SyntaxKind::Exclamation) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(bangIndex),
                                                                    "!"_zc);
      return ast::NodeId();
    }

    const size_t bodyOpen = bangIndex + 1;
    if (bodyOpen >= end || kindAt(bodyOpen) != ast::SyntaxKind::LeftBrace) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(bodyOpen),
                                                                    "{"_zc);
      return ast::NodeId();
    }

    const size_t bodyClose = findMatchingRightBrace(bodyOpen, end);
    if (bodyClose >= end) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(bodyOpen),
                                                                    "}"_zc);
      return ast::NodeId();
    }

    zc::Vector<ast::NodeId> rules;
    size_t cursor = bodyOpen + 1;
    while (cursor < bodyClose) {
      if (kindAt(cursor) == ast::SyntaxKind::Semicolon) {
        ++cursor;
        continue;
      }
      if (!isMacroGroupOpen(kindAt(cursor))) {
        diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
            diagnosticLoc(cursor));
        ++cursor;
        continue;
      }

      const size_t patternStart = cursor;
      const size_t patternClose = findMatchingMacroGroup(patternStart, bodyClose);
      if (patternClose >= bodyClose) {
        diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(
            diagnosticLoc(patternStart), macroGroupCloseLabel(kindAt(patternStart)));
        return ast::NodeId();
      }

      const size_t arrow =
          findTopLevelToken(patternClose + 1, bodyClose, ast::SyntaxKind::EqualsGreaterThan);
      if (arrow >= bodyClose) {
        diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(
            diagnosticLoc(patternClose + 1), "=>"_zc);
        return ast::NodeId();
      }

      size_t expansionStart = arrow + 1;
      while (expansionStart < bodyClose && kindAt(expansionStart) == ast::SyntaxKind::Semicolon) {
        ++expansionStart;
      }

      size_t expansionEnd = bodyClose;
      if (expansionStart < bodyClose && isMacroGroupOpen(kindAt(expansionStart))) {
        const size_t expansionClose = findMatchingMacroGroup(expansionStart, bodyClose);
        if (expansionClose >= bodyClose) {
          diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(
              diagnosticLoc(expansionStart), macroGroupCloseLabel(kindAt(expansionStart)));
          return ast::NodeId();
        }
        expansionEnd = expansionClose + 1;
      } else {
        const size_t semicolon =
            findTopLevelToken(expansionStart, bodyClose, ast::SyntaxKind::Semicolon);
        expansionEnd = semicolon < bodyClose ? semicolon : bodyClose;
      }

      ast::NodePayload rulePayload;
      writeNode(rulePayload, ast::kMacroRulePatternWord,
                makeEmptyMacroPattern(builder, patternStart, patternClose + 1));
      writeNode(rulePayload, ast::kMacroRuleExpandWord,
                makeEmptyMacroTokenTree(builder, expansionStart, expansionEnd));
      rules.add(builder.makeNode(ast::SyntaxKind::MacroRule, rangeFor(patternStart, expansionEnd),
                                 rulePayload));

      cursor = expansionEnd;
      if (cursor < bodyClose && kindAt(cursor) == ast::SyntaxKind::Semicolon) { ++cursor; }
    }

    ast::NodePayload payload;
    writeIdent(payload, ast::kMacroRulesDeclNameWord, internIdent(builder, nameIndex));
    writeNodeList(payload, ast::kMacroRulesDeclRulesFirstWord, ast::kMacroRulesDeclRulesSizeWord,
                  builder.makeList(rules.asPtr()));
    return builder.makeNode(ast::SyntaxKind::MacroRulesDecl, rangeFor(start, end), payload);
  }

  ast::NodeId parseFunctionDeclaration(ast::TreeBuilder& builder, size_t start, size_t end) const {
    const FunctionDeclarationParts parts = parseFunctionDeclarationParts(start, end);

    if (parts.nameIndex < end) { diagnoseDeclarationTypeParameterSyntax(parts.nameIndex + 1, end); }

    const size_t whereSearchStart =
        parts.closeParen < parts.headerEnd ? parts.closeParen + 1 : parts.headerEnd;
    const size_t where = findTopLevelIdentifierText(whereSearchStart, parts.headerEnd, "where"_zc);
    if (where < parts.headerEnd) {
      diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
          tokenAt(where).getLocation());
      return ast::NodeId();
    }

    ast::NodePayload payload;
    if (parts.nameIndex < end) {
      writeIdent(payload, ast::kFunctionDeclNameWord, internIdent(builder, parts.nameIndex));
    }
    writeNode(payload, ast::kFunctionDeclParamsIdWord,
              parseFunctionParameterList(builder, parts.openParen, parts.closeParen));
    if (parts.arrow < end) {
      const size_t retEnd = parts.raises < parts.headerEnd ? parts.raises : parts.headerEnd;
      writeNode(payload, ast::kFunctionDeclRetTyWord,
                parseTypeRange(builder, parts.arrow + 1, retEnd));
    }
    if (parts.raises < parts.headerEnd) {
      if (parts.raises + 1 >= parts.headerEnd ||
          kindAt(parts.raises + 1) == ast::SyntaxKind::Arrow) {
        diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(
            diagnosticLoc(parts.raises + 1));
        return ast::NodeId();
      }
      writeNode(payload, ast::kFunctionDeclRaisesTyWord,
                parseTypeRange(builder, parts.raises + 1, parts.headerEnd));
    }
    writeNode(payload, ast::kFunctionDeclBodyWord, parseBlock(builder, parts.bodyOpen, end));
    return builder.makeNode(ast::SyntaxKind::FunctionDecl, rangeFor(start, end), payload);
  }

  ast::NodeId parseNamedTypeDeclaration(ast::TreeBuilder& builder, size_t start, size_t end,
                                        ast::SyntaxKind kind) const {
    size_t nameIndex = end;
    for (size_t index = start + 1; index < end; ++index) {
      if (kindAt(index) == ast::SyntaxKind::Identifier) {
        nameIndex = index;
        break;
      }
    }

    if (nameIndex < end) { diagnoseDeclarationTypeParameterSyntax(nameIndex + 1, end); }

    size_t headerCursor = nameIndex < end ? nameIndex + 1 : end;
    if (headerCursor < end && kindAt(headerCursor) == ast::SyntaxKind::LessThan) {
      const size_t closeAngle = findMatchingAngleClose(headerCursor, end);
      headerCursor = closeAngle < end ? closeAngle + 1 : end;
    }
    if (kind == ast::SyntaxKind::StructDecl && headerCursor < end &&
        kindAt(headerCursor) == ast::SyntaxKind::LeftParen) {
      diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
          tokenAt(headerCursor).getLocation());
      return ast::NodeId();
    }

    const size_t bodyOpen = findTopLevelToken(headerCursor, end, ast::SyntaxKind::LeftBrace);
    const size_t headerEnd = bodyOpen < end ? bodyOpen : end;
    const size_t where = findTopLevelIdentifierText(headerCursor, headerEnd, "where"_zc);
    if (where < headerEnd) {
      diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
          tokenAt(where).getLocation());
      return ast::NodeId();
    }

    if (bodyOpen < end) {
      const size_t bodyClose = findMatchingRightBrace(bodyOpen, end);
      if (bodyClose < end) { diagnoseNamedTypeBody(bodyOpen, bodyClose, kind); }
    }

    ast::NodePayload payload;
    if (kind == ast::SyntaxKind::ClassDecl) {
      if (nameIndex < end) {
        writeIdent(payload, ast::kClassDeclNameWord, internIdent(builder, nameIndex));
      }
      payload.words[ast::kClassDeclExtensibilityWord] =
          static_cast<uint32_t>(ast::ClassExtensibility::Sealed);
      writeNode(payload, ast::kClassDeclMembersIdWord,
                makeEmptyClassMemberList(builder, rangeFor(start, end)));
    } else if (kind == ast::SyntaxKind::StructDecl) {
      if (nameIndex < end) {
        writeIdent(payload, ast::kStructDeclNameWord, internIdent(builder, nameIndex));
      }
      payload.words[ast::kStructDeclExtensibilityWord] =
          static_cast<uint32_t>(ast::ClassExtensibility::Sealed);
      writeNode(payload, ast::kStructDeclMembersIdWord,
                makeEmptyClassMemberList(builder, rangeFor(start, end)));
    } else if (kind == ast::SyntaxKind::InterfaceDecl) {
      if (nameIndex < end) {
        writeIdent(payload, ast::kInterfaceDeclNameWord, internIdent(builder, nameIndex));
      }
      writeNode(payload, ast::kInterfaceDeclMembersIdWord,
                makeEmptyClassMemberList(builder, rangeFor(start, end)));
    } else if (kind == ast::SyntaxKind::EnumDeclaration) {
      if (nameIndex < end) {
        writeIdent(payload, ast::kEnumDeclarationNameWord, internIdent(builder, nameIndex));
      }
      payload.words[ast::kEnumDeclarationExtensibilityWord] =
          static_cast<uint32_t>(ast::ClassExtensibility::Sealed);
      writeNode(payload, ast::kEnumDeclarationVariantsIdWord,
                makeEmptyEnumVariantList(builder, rangeFor(start, end)));
    }
    return builder.makeNode(kind, rangeFor(start, end), payload);
  }

  ast::NodeId parseErrorDeclaration(ast::TreeBuilder& builder, size_t start, size_t end) const {
    size_t nameIndex = end;
    for (size_t index = start + 1; index < end; ++index) {
      if (kindAt(index) == ast::SyntaxKind::Identifier) {
        nameIndex = index;
        break;
      }
    }

    ast::NodePayload payload;
    if (nameIndex < end) {
      writeIdent(payload, ast::kErrorDeclNameWord, internIdent(builder, nameIndex));
    }
    writeNode(payload, ast::kErrorDeclMembersIdWord,
              makeEmptyClassMemberList(builder, rangeFor(start, end)));
    return builder.makeNode(ast::SyntaxKind::ErrorDecl, rangeFor(start, end), payload);
  }

  ast::NodeId parseAliasDeclaration(ast::TreeBuilder& builder, size_t start, size_t end) const {
    if (kindAt(start) == ast::SyntaxKind::TypeKeyword) {
      diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
          tokenAt(start).getLocation());
      return ast::NodeId();
    }

    size_t nameIndex = end;
    for (size_t index = start + 1; index < end; ++index) {
      if (kindAt(index) == ast::SyntaxKind::Identifier) {
        nameIndex = index;
        break;
      }
    }

    if (nameIndex < end) { diagnoseDeclarationTypeParameterSyntax(nameIndex + 1, end); }

    const size_t equals = findTopLevelToken(start + 1, end, ast::SyntaxKind::Equals);
    ast::NodePayload payload;
    if (nameIndex < end) {
      writeIdent(payload, ast::kAliasDeclNameWord, internIdent(builder, nameIndex));
    }
    if (end == 0 || kindAt(end - 1) != ast::SyntaxKind::Semicolon) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(end), ";"_zc);
      return ast::NodeId();
    }
    if (equals < end) {
      const size_t errorCountBeforeTarget = diagnosticEngine.errorCount();
      const ast::NodeId target = parseTypeRange(builder, equals + 1, end);
      if (!target) {
        if (diagnosticEngine.errorCount() == errorCountBeforeTarget) {
          diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(equals + 1));
        }
        return ast::NodeId();
      }
      writeNode(payload, ast::kAliasDeclTargetWord, target);
    } else {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(nameIndex + 1),
                                                                    "="_zc);
      return ast::NodeId();
    }
    return builder.makeNode(ast::SyntaxKind::AliasDecl, rangeFor(start, end), payload);
  }

  ast::NodeId parseImplInterfaceBound(ast::TreeBuilder& builder, size_t start, size_t end) const {
    if (start >= end) {
      diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(start));
      return ast::NodeId();
    }

    const size_t pathEnd = findTypePathEnd(start, end);
    if (pathEnd == start) {
      diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(start));
      return ast::NodeId();
    }

    size_t boundEnd = pathEnd;
    if (pathEnd < end && kindAt(pathEnd) == ast::SyntaxKind::LessThan) {
      const size_t closeAngle = findMatchingAngleClose(pathEnd, end);
      if (closeAngle >= end || closeAngle + 1 != end) {
        diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(pathEnd),
                                                                      ">"_zc);
        return ast::NodeId();
      }
      boundEnd = end;
    }

    if (boundEnd != end) {
      const size_t bar = findTopLevelTypeToken(start, end, ast::SyntaxKind::Bar);
      const size_t ampersand = findTopLevelTypeToken(start, end, ast::SyntaxKind::Ampersand);
      const size_t comma = findTopLevelTypeToken(start, end, ast::SyntaxKind::Comma);
      size_t location = boundEnd;
      if (bar < end) {
        location = bar;
      } else if (ampersand < end) {
        location = ampersand;
      } else if (comma < end) {
        location = comma;
      }
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(location),
                                                                    "+"_zc);
      return ast::NodeId();
    }

    const ast::NodeId iface = parseTypeRange(builder, start, end);
    if (!iface) {
      diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(start));
    }
    return iface;
  }

  ast::NodeId makeImplIfaceList(ast::TreeBuilder& builder, size_t start, size_t end) const {
    zc::Vector<ast::NodeId> ifaces;
    size_t cursor = start;
    while (cursor < end) {
      const size_t plus = findTopLevelTypeToken(cursor, end, ast::SyntaxKind::Plus);
      const size_t itemEnd = plus < end ? plus : end;
      if (cursor >= itemEnd) {
        diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(cursor));
        return ast::NodeId();
      }
      const ast::NodeId iface = parseImplInterfaceBound(builder, cursor, itemEnd);
      if (!iface) { return ast::NodeId(); }
      ifaces.add(iface);
      cursor = plus < end ? plus + 1 : end;
    }
    if (ifaces.size() == 0) {
      diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(start));
      return ast::NodeId();
    }

    ast::NodePayload payload;
    payload.words[ast::kImplIfaceListNIfacesWord] = static_cast<uint32_t>(ifaces.size());
    writeNodeList(payload, ast::kImplIfaceListIfacesFirstWord, ast::kImplIfaceListIfacesSizeWord,
                  builder.makeList(ifaces.asPtr()));
    return builder.makeNode(ast::SyntaxKind::ImplIfaceList, rangeFor(start, end), payload);
  }

  ast::NodeId parseStandaloneImplDeclaration(ast::TreeBuilder& builder, size_t start,
                                             size_t end) const {
    bool isUnsafe = false;
    size_t implIndex = start;
    if (isSoftKeyword(implIndex, "unsafe"_zc)) {
      isUnsafe = true;
      ++implIndex;
    }

    if (!isSoftKeyword(implIndex, "impl"_zc)) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(implIndex),
                                                                    "impl"_zc);
      return ast::NodeId();
    }

    const size_t bodyOpen = findTopLevelToken(implIndex + 1, end, ast::SyntaxKind::LeftBrace);
    const size_t semi = findTopLevelToken(implIndex + 1, end, ast::SyntaxKind::Semicolon);
    const size_t headerEnd = bodyOpen < end ? bodyOpen : (semi < end ? semi : end);
    const size_t forIndex =
        findTopLevelTypeToken(implIndex + 1, headerEnd, ast::SyntaxKind::ForKeyword);
    if (forIndex >= headerEnd) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(headerEnd),
                                                                    "for"_zc);
      return ast::NodeId();
    }
    if (implIndex + 1 >= forIndex) {
      diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(implIndex + 1));
      return ast::NodeId();
    }
    if (forIndex + 1 >= headerEnd) {
      diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(forIndex + 1));
      return ast::NodeId();
    }
    if (bodyOpen >= end && semi >= end) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(headerEnd),
                                                                    "{"_zc);
      return ast::NodeId();
    }

    const ast::NodeId ifaces = makeImplIfaceList(builder, implIndex + 1, forIndex);
    if (!ifaces) { return ast::NodeId(); }
    const ast::NodeId forTy = parseTypeRange(builder, forIndex + 1, headerEnd);
    if (!forTy) {
      diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(forIndex + 1));
      return ast::NodeId();
    }

    ast::NodePayload payload;
    payload.words[ast::kStandaloneImplDeclIsUnsafeWord] = isUnsafe ? 1 : 0;
    writeNode(payload, ast::kStandaloneImplDeclIfacesIdWord, ifaces);
    writeNode(payload, ast::kStandaloneImplDeclForTyWord, forTy);
    if (bodyOpen < end) {
      writeNode(payload, ast::kStandaloneImplDeclMembersIdWord,
                makeEmptyClassMemberList(builder, rangeFor(bodyOpen, end)));
    }
    return builder.makeNode(ast::SyntaxKind::StandaloneImplDecl, rangeFor(start, end), payload);
  }

  ast::NodeId parseExpressionStatement(ast::TreeBuilder& builder, size_t start, size_t end) const {
    if (!requireTrailingSemicolon(start, end)) { return ast::NodeId(); }

    ast::NodePayload payload;
    writeNode(payload, ast::kExpressionStatementExpressionWord,
              parseRequiredExpression(builder, start, end));
    return builder.makeNode(ast::SyntaxKind::ExpressionStatement, rangeFor(start, end), payload);
  }

  ast::NodeId parseExpressionStatementWithoutSemicolon(ast::TreeBuilder& builder, size_t start,
                                                       size_t end) const {
    const ast::NodeId expr = parseRequiredExpression(builder, start, end);
    if (!expr) { return ast::NodeId(); }

    ast::NodePayload payload;
    writeNode(payload, ast::kExpressionStatementExpressionWord, expr);
    return builder.makeNode(ast::SyntaxKind::ExpressionStatement, rangeFor(start, end), payload);
  }

  ast::NodeId parseSpawnStatement(ast::TreeBuilder& builder, size_t start, size_t end) const {
    size_t exprEnd = end;
    if (start < exprEnd && kindAt(exprEnd - 1) == ast::SyntaxKind::Semicolon) { --exprEnd; }

    const ast::NodeId expr = parseRequiredExpression(builder, start, exprEnd);
    if (!expr) { return ast::NodeId(); }

    ast::NodePayload payload;
    writeNode(payload, ast::kExpressionStatementExpressionWord, expr);
    return builder.makeNode(ast::SyntaxKind::ExpressionStatement, rangeFor(start, end), payload);
  }

  ast::NodeId parseSourceElementOfKind(ast::TreeBuilder& builder, size_t start, size_t end,
                                       ast::SyntaxKind kind) const {
    if (start >= end) { return ast::NodeId(); }

    switch (kind) {
      case ast::SyntaxKind::ModuleDeclaration:
        return parseModuleDeclaration(builder, start, end);
      case ast::SyntaxKind::ImportDeclaration:
        return parseImportDeclaration(builder, start, end);
      case ast::SyntaxKind::ExportDeclaration:
        return parseExportDeclaration(builder, start, end);
      case ast::SyntaxKind::LetStmt:
        return parseLetStatement(builder, start, end);
      case ast::SyntaxKind::FunctionDecl:
        return parseFunctionDeclaration(builder, start, end);
      case ast::SyntaxKind::ClassDecl:
      case ast::SyntaxKind::StructDecl:
      case ast::SyntaxKind::InterfaceDecl:
      case ast::SyntaxKind::EnumDeclaration:
        return parseNamedTypeDeclaration(builder, start, end, kind);
      case ast::SyntaxKind::ErrorDecl:
        return parseErrorDeclaration(builder, start, end);
      case ast::SyntaxKind::AliasDecl:
        return parseAliasDeclaration(builder, start, end);
      case ast::SyntaxKind::MacroRulesDecl:
        return parseMacroRulesDeclaration(builder, start, end);
      case ast::SyntaxKind::ExternBlock:
        return parseExternBlockDeclaration(builder, start, end);
      case ast::SyntaxKind::StandaloneImplDecl:
        return parseStandaloneImplDeclaration(builder, start, end);
      case ast::SyntaxKind::ReturnStmt:
        return parseReturnStatement(builder, start, end);
      case ast::SyntaxKind::SuspendStatement:
        return parseSuspendStatement(builder, start, end);
      case ast::SyntaxKind::BlockStmt:
        return parseBlock(builder, start, end);
      case ast::SyntaxKind::IfStmt:
        return parseIfStatement(builder, start, end);
      case ast::SyntaxKind::MatchStmt:
        return parseMatchStatement(builder, start, end);
      case ast::SyntaxKind::WhileStmt:
        return parseWhileStatement(builder, start, end);
      case ast::SyntaxKind::ForStmt:
        return parseForStatement(builder, start, end);
      case ast::SyntaxKind::ForInStatement:
        return parseForInStatement(builder, start, end);
      case ast::SyntaxKind::BreakStmt:
        return parseBreakStatement(builder, start, end);
      case ast::SyntaxKind::ContinueStatement:
        return parseContinueStatement(builder, start, end);
      case ast::SyntaxKind::LabeledStatement:
        return parseLabeledStatement(builder, start, end);
      case ast::SyntaxKind::DoWhileStatement:
        return parseDoWhileStatement(builder, start, end);
      case ast::SyntaxKind::EmptyStatement:
        return builder.makeNode(ast::SyntaxKind::EmptyStatement, rangeFor(start, end));
      case ast::SyntaxKind::DebuggerStatement:
        if (!requireTrailingSemicolon(start, end)) { return ast::NodeId(); }
        return builder.makeNode(ast::SyntaxKind::DebuggerStatement, rangeFor(start, end));
      case ast::SyntaxKind::ExpressionStatement:
        if (kindAt(start) == ast::SyntaxKind::SpawnKeyword) {
          return parseSpawnStatement(builder, start, end);
        }
        return parseExpressionStatement(builder, start, end);
      default:
        return parseExpressionStatement(builder, start, end);
    }
  }

  bool canContinueLetInitializerBefore(size_t index) const {
    if (index == 0) { return false; }

    const ast::SyntaxKind kind = kindAt(index);
    const ast::SyntaxKind previous = kindAt(index - 1);
    if ((kind == ast::SyntaxKind::FunKeyword || kind == ast::SyntaxKind::ImportKeyword ||
         kind == ast::SyntaxKind::SpawnKeyword) &&
        previous == ast::SyntaxKind::Equals) {
      return true;
    }
    if (kind == ast::SyntaxKind::SpawnKeyword &&
        (binaryPrecedence(previous) > 0 || isAssignmentOperator(previous) ||
         previous == ast::SyntaxKind::Question || previous == ast::SyntaxKind::Colon ||
         previous == ast::SyntaxKind::Comma || previous == ast::SyntaxKind::LeftParen ||
         previous == ast::SyntaxKind::LeftBracket)) {
      return true;
    }
    if (lexer::isKeyword(kind) &&
        (previous == ast::SyntaxKind::Period || previous == ast::SyntaxKind::QuestionDot)) {
      return true;
    }
    return false;
  }

  ast::Tree buildTree() {
    ast::TreeBuilder builder;
    ast::NodeId moduleNode;
    zc::Vector<ast::NodeId> statements;
    bool firstSourceElement = true;

    const size_t count = tokenCountWithoutEof();
    TokenCursor cursor = tokenCursorAt(0);
    while (cursor.position() < count) {
      const size_t index = cursor.position();
      const SourceElementParseResult elementResult = parseSourceElement(builder, cursor, count);
      const size_t end = elementResult.boundary.end;
      const size_t elementStart = elementResult.boundary.head;
      const ast::SyntaxKind first =
          elementStart < count ? kindAt(elementStart) : ast::SyntaxKind::EndOfFile;
      if (outerAttributePrefixContainsZomCfg(index, end) &&
          !isTopLevelCfgAttributeTarget(elementResult.boundary.kind)) {
        diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(
            tokenAt(elementStart).getLocation(), "cfg-gated declaration or block"_zc);
      }

      if (first == ast::SyntaxKind::ModuleKeyword && firstSourceElement && !moduleNode) {
        moduleNode = elementResult.node;
      } else {
        if (first == ast::SyntaxKind::ModuleKeyword) {
          diagnosticEngine.diagnose<diagnostics::DiagID::ModuleDeclarationMustBeFirst>(
              tokenAt(elementStart).getLocation());
        }
        if (elementResult.node) {
          statements.add(makeStatementListItem(builder, elementResult.node, rangeFor(index, end),
                                               elementResult.attrs));
        }
      }

      firstSourceElement = false;
      if (cursor.position() <= index) { cursor.moveTo(index + 1); }
    }

    const ast::NodeList statementList = builder.makeList(statements.asPtr());
    const ast::NodeId root =
        builder.makeNode(ast::SyntaxKind::SourceFile, rangeFor(0, tokens.size()),
                         makeSourceFilePayload(builder.internString(context.fileIdentifier()),
                                               moduleNode, statementList));
    builder.setRoot(root);
    return builder.finish();
  }
};

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
