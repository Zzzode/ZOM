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

#include "zomlang/compiler/parser/parser.h"

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
#include "zomlang/compiler/source/manager.h"
#include "zomlang/compiler/trace/trace.h"

namespace zomlang {
namespace compiler {
namespace parser {

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
    case ast::SyntaxKind::BigIntLiteral:
    case ast::SyntaxKind::FloatLiteral:
    case ast::SyntaxKind::StringLiteral:
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

bool isTopLevelStart(ast::SyntaxKind kind) {
  switch (kind) {
    case ast::SyntaxKind::ModuleKeyword:
    case ast::SyntaxKind::ImportKeyword:
    case ast::SyntaxKind::ExportKeyword:
    case ast::SyntaxKind::LetKeyword:
    case ast::SyntaxKind::ConstKeyword:
    case ast::SyntaxKind::FunKeyword:
    case ast::SyntaxKind::ClassKeyword:
    case ast::SyntaxKind::StructKeyword:
    case ast::SyntaxKind::InterfaceKeyword:
    case ast::SyntaxKind::EnumKeyword:
    case ast::SyntaxKind::ErrorKeyword:
    case ast::SyntaxKind::TypeKeyword:
    case ast::SyntaxKind::IfKeyword:
    case ast::SyntaxKind::MatchKeyword:
    case ast::SyntaxKind::WhileKeyword:
    case ast::SyntaxKind::DoKeyword:
    case ast::SyntaxKind::ForKeyword:
    case ast::SyntaxKind::BreakKeyword:
    case ast::SyntaxKind::ContinueKeyword:
    case ast::SyntaxKind::ReturnKeyword:
    case ast::SyntaxKind::DebuggerKeyword:
    case ast::SyntaxKind::Semicolon:
      return true;
    default:
      return false;
  }
}

bool canOwnBracedBody(ast::SyntaxKind kind) {
  switch (kind) {
    case ast::SyntaxKind::FunKeyword:
    case ast::SyntaxKind::ClassKeyword:
    case ast::SyntaxKind::StructKeyword:
    case ast::SyntaxKind::InterfaceKeyword:
    case ast::SyntaxKind::EnumKeyword:
    case ast::SyntaxKind::ErrorKeyword:
    case ast::SyntaxKind::IfKeyword:
    case ast::SyntaxKind::MatchKeyword:
    case ast::SyntaxKind::WhileKeyword:
    case ast::SyntaxKind::DoKeyword:
    case ast::SyntaxKind::ForKeyword:
    case ast::SyntaxKind::LeftBrace:
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

struct Parser::Impl {
  Impl(const source::SourceManager& sourceMgr, diagnostics::DiagnosticEngine& diagnosticEngine,
       const basic::LangOptions& langOpts, basic::StringPool& stringPool,
       const source::BufferId& bufferId)
      : sourceMgr(sourceMgr),
        diagnosticEngine(diagnosticEngine),
        lexer(sourceMgr, diagnosticEngine, langOpts, stringPool, bufferId),
        bufferId(bufferId) {}

  const source::SourceManager& sourceMgr;
  diagnostics::DiagnosticEngine& diagnosticEngine;
  lexer::Lexer lexer;
  lexer::Token token;
  source::BufferId bufferId;
  zc::Vector<lexer::Token> tokens;

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
  }

  size_t tokenCountWithoutEof() const {
    if (tokens.size() == 0) { return 0; }
    return tokens.size() - 1;
  }

  const lexer::Token& tokenAt(size_t index) const {
    ZC_IREQUIRE(index < tokens.size(), "parser token index outside token stream");
    return tokens[index];
  }

  ast::SyntaxKind kindAt(size_t index) const { return tokenAt(index).getKind(); }

  source::SourceRange rangeFor(size_t start, size_t end) const {
    if (tokens.size() == 0) {
      const source::SourceLoc loc = sourceMgr.getLocForBufferStart(bufferId);
      return source::SourceRange(loc, loc);
    }

    const size_t safeStart = start < tokens.size() ? start : tokens.size() - 1;
    const size_t safeEnd = end > start && end <= tokens.size() ? end - 1 : safeStart;
    return source::SourceRange(tokenAt(safeStart).getRange().getStart(),
                               tokenAt(safeEnd).getRange().getEnd());
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

  size_t findTopLevelCommaOrEnd(size_t start, size_t end) const {
    const size_t comma = findTopLevelToken(start, end, ast::SyntaxKind::Comma);
    return comma < end ? comma : end;
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
      } else if (kind == ast::SyntaxKind::GreaterThan) {
        if (angleDepth > 0) { --angleDepth; }
      }
    }
    return end;
  }

  size_t findTopLevelTypeCommaOrEnd(size_t start, size_t end) const {
    const size_t comma = findTopLevelTypeToken(start, end, ast::SyntaxKind::Comma);
    return comma < end ? comma : end;
  }

  void addNodeIfPresent(zc::Vector<ast::NodeId>& nodes, ast::NodeId node) const {
    if (node) { nodes.add(node); }
  }

  ast::IdentList makeIdentList(ast::TreeBuilder& builder, size_t start, size_t end) const {
    zc::Vector<ast::IdentId> segments;
    for (size_t index = start; index < end; ++index) {
      if (kindAt(index) == ast::SyntaxKind::Identifier ||
          kindAt(index) == ast::SyntaxKind::ThisKeyword) {
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

      if (cursor + 1 < end && kindAt(cursor) == ast::SyntaxKind::Colon &&
          kindAt(cursor + 1) == ast::SyntaxKind::Colon) {
        cursor += 2;
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
    return text == "inline"_zc || text == "deprecated"_zc || text == "cold"_zc;
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
      size_t argStart = pathEnd + 1;
      while (argStart < argsEnd) {
        const size_t comma = findTopLevelCommaOrEnd(argStart, argsEnd);
        if (argStart < comma) {
          addNodeIfPresent(args, parseExpressionRange(builder, argStart, comma));
        }
        argStart = comma < argsEnd ? comma + 1 : argsEnd;
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

      size_t itemStart = cursor + 2;
      if (itemStart >= closeBracket) {
        diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(itemStart),
                                                                      "attribute path"_zc);
      }
      while (itemStart < closeBracket) {
        const size_t comma = findTopLevelCommaOrEnd(itemStart, closeBracket);
        if (itemStart < comma) {
          addNodeIfPresent(attrs, parseAttribute(builder, itemStart, comma));
        }
        itemStart = comma < closeBracket ? comma + 1 : closeBracket;
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

      size_t itemStart = cursor + 2;
      while (itemStart < closeBracket) {
        const size_t comma = findTopLevelCommaOrEnd(itemStart, closeBracket);
        const size_t pathEnd = findAttributePathEnd(itemStart, comma);
        if (pathEnd > itemStart && isZomCfgAttributePath(itemStart, pathEnd)) { return true; }
        itemStart = comma < closeBracket ? comma + 1 : closeBracket;
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

  source::SourceLoc diagnosticLoc(size_t index) const {
    if (index < tokens.size()) { return tokenAt(index).getLocation(); }
    ZC_IREQUIRE(tokens.size() != 0, "parser diagnostics require a token stream");
    return tokenAt(tokens.size() - 1).getLocation();
  }

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

  ast::NodeId parseRequiredExpression(ast::TreeBuilder& builder, size_t start, size_t end) const {
    const ast::NodeId expr = parseExpressionRange(builder, start, end);
    if (!expr) { diagnoseExpressionExpected(start); }
    return expr;
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

  bool looksLikeObjectLiteralExpression(size_t start, size_t end) const {
    if (!rangeIsWrapped(start, end, ast::SyntaxKind::LeftBrace, ast::SyntaxKind::RightBrace)) {
      return false;
    }
    return findTopLevelToken(start + 1, end - 1, ast::SyntaxKind::Colon) < end - 1 ||
           findTopLevelToken(start + 1, end - 1, ast::SyntaxKind::DotDotDot) < end - 1;
  }

  void diagnoseTokenPatterns() {
    int32_t braceDepth = 0;
    bool sawTopLevelBlock = false;
    bool waitingForInterfaceBody = false;
    int32_t interfaceBodyDepth = -1;
    int32_t typeLiteralBraceDepth = -1;
    int32_t bindingPatternBraceDepth = -1;

    const size_t count = tokenCountWithoutEof();
    for (size_t i = 0; i < count; ++i) {
      const lexer::Token& current = tokenAt(i);
      const ast::SyntaxKind kind = current.getKind();
      const ast::SyntaxKind next = i + 1 < count ? kindAt(i + 1) : ast::SyntaxKind::EndOfFile;
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
      if (waitingForInterfaceBody && braceDepth == 0 && kind != ast::SyntaxKind::InterfaceKeyword &&
          kind != ast::SyntaxKind::Identifier && kind != ast::SyntaxKind::ExtendsKeyword &&
          kind != ast::SyntaxKind::Comma && kind != ast::SyntaxKind::LessThan &&
          kind != ast::SyntaxKind::GreaterThan && kind != ast::SyntaxKind::LeftBrace) {
        diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(current.getLocation(),
                                                                      "{"_zc);
        waitingForInterfaceBody = false;
      }

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

      if (braceDepth > 0 && isInvalidObjectLiteralPropertyName(kind) &&
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

      if (braceDepth > 0 && kind == ast::SyntaxKind::Identifier && i + 3 < count &&
          kindAt(i + 1) == ast::SyntaxKind::Colon && kindAt(i + 2) != ast::SyntaxKind::Semicolon &&
          kindAt(i + 2) != ast::SyntaxKind::RightBrace &&
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

  ast::SyntaxKind classifyStatement(size_t start, size_t end) const {
    const size_t head = effectiveStatementStart(start, end);
    const ast::SyntaxKind first = kindAt(head);
    switch (first) {
      case ast::SyntaxKind::ModuleKeyword:
        return ast::SyntaxKind::ModuleDeclaration;
      case ast::SyntaxKind::ImportKeyword:
        return ast::SyntaxKind::ImportDeclaration;
      case ast::SyntaxKind::ExportKeyword:
        return ast::SyntaxKind::ExportDeclaration;
      case ast::SyntaxKind::LetKeyword:
      case ast::SyntaxKind::ConstKeyword:
        return ast::SyntaxKind::LetStmt;
      case ast::SyntaxKind::FunKeyword:
        return ast::SyntaxKind::FunctionDecl;
      case ast::SyntaxKind::ClassKeyword:
        return ast::SyntaxKind::ClassDecl;
      case ast::SyntaxKind::StructKeyword:
        return ast::SyntaxKind::StructDecl;
      case ast::SyntaxKind::InterfaceKeyword:
        return ast::SyntaxKind::InterfaceDecl;
      case ast::SyntaxKind::EnumKeyword:
        return ast::SyntaxKind::EnumDeclaration;
      case ast::SyntaxKind::ErrorKeyword:
        return ast::SyntaxKind::ErrorDecl;
      case ast::SyntaxKind::TypeKeyword:
      case ast::SyntaxKind::AliasKeyword:
        return ast::SyntaxKind::AliasDecl;
      case ast::SyntaxKind::IfKeyword:
        return ast::SyntaxKind::IfStmt;
      case ast::SyntaxKind::MatchKeyword:
        return ast::SyntaxKind::MatchStmt;
      case ast::SyntaxKind::WhileKeyword:
        return ast::SyntaxKind::WhileStmt;
      case ast::SyntaxKind::DoKeyword:
        return ast::SyntaxKind::DoWhileStatement;
      case ast::SyntaxKind::ForKeyword:
        for (size_t i = head + 1; i < end; ++i) {
          if (kindAt(i) == ast::SyntaxKind::InKeyword) { return ast::SyntaxKind::ForInStatement; }
          if (kindAt(i) == ast::SyntaxKind::Semicolon) { break; }
        }
        return ast::SyntaxKind::ForStmt;
      case ast::SyntaxKind::BreakKeyword:
        return ast::SyntaxKind::BreakStmt;
      case ast::SyntaxKind::ContinueKeyword:
        return ast::SyntaxKind::ContinueStatement;
      case ast::SyntaxKind::ReturnKeyword:
        return ast::SyntaxKind::ReturnStmt;
      case ast::SyntaxKind::DebuggerKeyword:
        return ast::SyntaxKind::DebuggerStatement;
      case ast::SyntaxKind::Semicolon:
        return ast::SyntaxKind::EmptyStatement;
      case ast::SyntaxKind::LeftBrace:
        if (looksLikeObjectLiteralExpression(head, end)) {
          return ast::SyntaxKind::ExpressionStatement;
        }
        return ast::SyntaxKind::BlockStmt;
      case ast::SyntaxKind::Identifier:
        if (head + 1 < end && kindAt(head + 1) == ast::SyntaxKind::Colon) {
          return ast::SyntaxKind::LabeledStatement;
        }
        return ast::SyntaxKind::ExpressionStatement;
      default:
        return ast::SyntaxKind::ExpressionStatement;
    }
  }

  size_t effectiveStatementStart(size_t start, size_t end) const {
    size_t head = skipOuterAttributePrefix(start, end);
    while (head < end && isDeclarationModifier(kindAt(head))) { ++head; }
    return head < end ? head : start;
  }

  ast::NodeId parseTypeList(ast::TreeBuilder& builder, size_t start, size_t end,
                            ast::SyntaxKind containerKind) const {
    zc::Vector<ast::NodeId> types;
    size_t cursor = start;
    while (cursor < end) {
      const size_t comma = findTopLevelTypeCommaOrEnd(cursor, end);
      if (cursor < comma) { addNodeIfPresent(types, parseTypeRange(builder, cursor, comma)); }
      cursor = comma < end ? comma + 1 : end;
    }

    const ast::NodeList list = builder.makeList(types.asPtr());
    ast::NodePayload payload;
    if (containerKind == ast::SyntaxKind::UnionTypeExpr) {
      writeNodeList(payload, ast::kUnionTypeExprAltsFirstWord, ast::kUnionTypeExprAltsSizeWord,
                    list);
    } else {
      writeNodeList(payload, ast::kIntersectionTypeExprAltsFirstWord,
                    ast::kIntersectionTypeExprAltsSizeWord, list);
    }
    return builder.makeNode(containerKind, rangeFor(start, end), payload);
  }

  size_t findMatchingAngleClose(size_t openIndex, size_t limit) const {
    if (openIndex >= limit || kindAt(openIndex) != ast::SyntaxKind::LessThan) { return limit; }

    int32_t depth = 0;
    for (size_t index = openIndex; index < limit; ++index) {
      const ast::SyntaxKind kind = kindAt(index);
      if (kind == ast::SyntaxKind::LessThan) { ++depth; }
      if (kind == ast::SyntaxKind::GreaterThan) {
        --depth;
        if (depth == 0) { return index; }
      }
    }
    return limit;
  }

  size_t findFunctionTypeParameterOpen(size_t start, size_t end) const {
    if (start >= end) { return end; }
    if (kindAt(start) == ast::SyntaxKind::LeftParen) { return start; }
    if (kindAt(start) == ast::SyntaxKind::FunKeyword) {
      size_t cursor = start + 1;
      if (cursor < end && kindAt(cursor) == ast::SyntaxKind::LessThan) {
        const size_t closeAngle = findMatchingAngleClose(cursor, end);
        cursor = closeAngle < end ? closeAngle + 1 : end;
      }
      if (cursor < end && kindAt(cursor) == ast::SyntaxKind::LeftParen) { return cursor; }
    }
    if (kindAt(start) == ast::SyntaxKind::LessThan) {
      const size_t closeAngle = findMatchingAngleClose(start, end);
      if (closeAngle + 1 < end && kindAt(closeAngle + 1) == ast::SyntaxKind::LeftParen) {
        return closeAngle + 1;
      }
    }
    return end;
  }

  size_t findTopLevelTypeColonOrEnd(size_t start, size_t end) const {
    const size_t colon = findTopLevelTypeToken(start, end, ast::SyntaxKind::Colon);
    return colon < end ? colon : end;
  }

  ast::NodeList parseFunctionTypeParameters(ast::TreeBuilder& builder, size_t start,
                                            size_t end) const {
    zc::Vector<ast::NodeId> params;
    size_t cursor = start;
    while (cursor < end) {
      const size_t comma = findTopLevelTypeCommaOrEnd(cursor, end);
      if (cursor < comma) {
        const size_t colon = findTopLevelTypeColonOrEnd(cursor, comma);
        const size_t typeStart = colon < comma ? colon + 1 : cursor;
        addNodeIfPresent(params, parseTypeRange(builder, typeStart, comma));
      }
      cursor = comma < end ? comma + 1 : end;
    }
    return builder.makeList(params.asPtr());
  }

  ast::NodeId parseTupleTypeRange(ast::TreeBuilder& builder, size_t start, size_t end) const {
    zc::Vector<ast::NodeId> elems;
    bool hasTrailingComma = false;
    size_t cursor = start + 1;
    while (cursor + 1 < end) {
      const size_t comma = findTopLevelTypeCommaOrEnd(cursor, end - 1);
      if (cursor < comma) {
        const size_t colon = findTopLevelTypeColonOrEnd(cursor, comma);
        const size_t typeStart = colon < comma ? colon + 1 : cursor;
        addNodeIfPresent(elems, parseTypeRange(builder, typeStart, comma));
      }
      if (comma < end - 1 && comma + 1 >= end - 1) { hasTrailingComma = true; }
      cursor = comma < end - 1 ? comma + 1 : end - 1;
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

  ast::NodeId parseFunctionTypeRange(ast::TreeBuilder& builder, size_t start, size_t end) const {
    const size_t openParen = findFunctionTypeParameterOpen(start, end);
    if (openParen >= end) { return ast::NodeId(); }

    const size_t closeParen = findMatchingRightParen(openParen, end);
    if (closeParen + 1 >= end || kindAt(closeParen + 1) != ast::SyntaxKind::Arrow) {
      return ast::NodeId();
    }

    const size_t retStart = closeParen + 2;
    if (retStart >= end) {
      diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(
          tokenAt(closeParen + 1).getLocation());
      return ast::NodeId();
    }

    const size_t raises = findTopLevelTypeToken(retStart, end, ast::SyntaxKind::RaisesKeyword);
    const size_t retEnd = raises < end ? raises : end;
    const ast::NodeId retTy = parseTypeRange(builder, retStart, retEnd);
    if (!retTy) {
      diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(tokenAt(retStart).getLocation());
      return ast::NodeId();
    }

    ast::NodeId raisesTy;
    if (raises < end) {
      raisesTy = parseTypeRange(builder, raises + 1, end);
      if (!raisesTy) {
        diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(tokenAt(raises).getLocation());
        return ast::NodeId();
      }
    }

    ast::NodePayload payload;
    writeNodeList(payload, ast::kFunctionTypeExprParamsFirstWord,
                  ast::kFunctionTypeExprParamsSizeWord,
                  parseFunctionTypeParameters(builder, openParen + 1, closeParen));
    writeNode(payload, ast::kFunctionTypeExprRetTyWord, retTy);
    if (raisesTy) { writeNode(payload, ast::kFunctionTypeExprRaisesWord, raisesTy); }
    return builder.makeNode(ast::SyntaxKind::FunctionTypeExpr, rangeFor(start, end), payload);
  }

  size_t findTopLevelObjectTypeMemberEnd(size_t start, size_t end) const {
    const size_t comma = findTopLevelTypeToken(start, end, ast::SyntaxKind::Comma);
    const size_t semi = findTopLevelTypeToken(start, end, ast::SyntaxKind::Semicolon);
    if (comma < end && semi < end) { return comma < semi ? comma : semi; }
    if (comma < end) { return comma; }
    if (semi < end) { return semi; }
    return end;
  }

  ast::NodeId parseObjectTypeRange(ast::TreeBuilder& builder, size_t start, size_t end) const {
    zc::Vector<ast::NodeId> members;
    size_t cursor = start + 1;
    while (cursor + 1 < end) {
      const size_t memberEnd = findTopLevelObjectTypeMemberEnd(cursor, end - 1);
      if (cursor < memberEnd) {
        bool isMut = false;
        size_t nameIndex = cursor;
        if (kindAt(nameIndex) == ast::SyntaxKind::Identifier &&
            tokenAt(nameIndex).getValue() == "mut"_zc) {
          isMut = true;
          ++nameIndex;
        }

        if (nameIndex >= memberEnd || kindAt(nameIndex) != ast::SyntaxKind::Identifier) {
          diagnosticEngine.diagnose<diagnostics::DiagID::IdentifierExpected>(
              diagnosticLoc(nameIndex));
          return ast::NodeId();
        }

        size_t typeStart = nameIndex + 1;
        bool isOptional = false;
        bool hasColon = false;
        if (typeStart < memberEnd && kindAt(typeStart) == ast::SyntaxKind::ErrorDefault) {
          isOptional = true;
          hasColon = true;
          ++typeStart;
        } else if (typeStart < memberEnd && kindAt(typeStart) == ast::SyntaxKind::Question) {
          isOptional = true;
          ++typeStart;
        }
        if (typeStart < memberEnd && kindAt(typeStart) == ast::SyntaxKind::Colon) {
          hasColon = true;
          ++typeStart;
        }
        if (!hasColon) {
          diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(typeStart),
                                                                        ":"_zc);
          return ast::NodeId();
        }

        const ast::NodeId ty = parseTypeRange(builder, typeStart, memberEnd);
        if (!ty) {
          diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(typeStart));
          return ast::NodeId();
        }

        ast::NodePayload memberPayload;
        writeIdent(memberPayload, ast::kObjectTypeMemberNameWord, internIdent(builder, nameIndex));
        writeNode(memberPayload, ast::kObjectTypeMemberTyWord, ty);
        memberPayload.words[ast::kObjectTypeMemberIsMutWord] = isMut ? 1 : 0;
        memberPayload.words[ast::kObjectTypeMemberIsOptionalWord] = isOptional ? 1 : 0;
        members.add(builder.makeNode(ast::SyntaxKind::ObjectTypeMember, rangeFor(cursor, memberEnd),
                                     memberPayload));
      }
      cursor = memberEnd < end - 1 ? memberEnd + 1 : end - 1;
    }

    ast::NodePayload payload;
    writeNodeList(payload, ast::kObjectTypeExprMembersFirstWord,
                  ast::kObjectTypeExprMembersSizeWord, builder.makeList(members.asPtr()));
    return builder.makeNode(ast::SyntaxKind::ObjectTypeExpr, rangeFor(start, end), payload);
  }

  ast::NodeId parseTypeRange(ast::TreeBuilder& builder, size_t start, size_t end) const {
    while (start < end && kindAt(end - 1) == ast::SyntaxKind::Semicolon) { --end; }
    if (start >= end) { return ast::NodeId(); }

    const ast::NodeId functionType = parseFunctionTypeRange(builder, start, end);
    if (functionType) { return functionType; }

    if (rangeIsWrapped(start, end, ast::SyntaxKind::LeftParen, ast::SyntaxKind::RightParen)) {
      if (start + 1 == end - 1 ||
          findTopLevelTypeToken(start + 1, end - 1, ast::SyntaxKind::Comma) < end - 1) {
        return parseTupleTypeRange(builder, start, end);
      }
      return parseTypeRange(builder, start + 1, end - 1);
    }

    if (kindAt(start) == ast::SyntaxKind::TypeOfKeyword && start + 1 < end) {
      ast::NodePayload payload;
      writeNode(payload, ast::kTypeQueryExprPathWord, makeModulePath(builder, start + 1, end));
      return builder.makeNode(ast::SyntaxKind::TypeQueryExpr, rangeFor(start, end), payload);
    }

    if (rangeIsWrapped(start, end, ast::SyntaxKind::LeftBrace, ast::SyntaxKind::RightBrace)) {
      return parseObjectTypeRange(builder, start, end);
    }

    const size_t unionOp = findTopLevelTypeToken(start, end, ast::SyntaxKind::Bar);
    if (unionOp < end) {
      zc::Vector<ast::NodeId> alts;
      size_t cursor = start;
      while (cursor < end) {
        const size_t split = findTopLevelTypeToken(cursor, end, ast::SyntaxKind::Bar);
        const size_t segmentEnd = split < end ? split : end;
        if (cursor < segmentEnd) {
          addNodeIfPresent(alts, parseTypeRange(builder, cursor, segmentEnd));
        }
        cursor = split < end ? split + 1 : end;
      }
      const ast::NodeList list = builder.makeList(alts.asPtr());
      ast::NodePayload payload;
      writeNodeList(payload, ast::kUnionTypeExprAltsFirstWord, ast::kUnionTypeExprAltsSizeWord,
                    list);
      return builder.makeNode(ast::SyntaxKind::UnionTypeExpr, rangeFor(start, end), payload);
    }

    const size_t intersectionOp = findTopLevelTypeToken(start, end, ast::SyntaxKind::Ampersand);
    if (intersectionOp < end) {
      zc::Vector<ast::NodeId> alts;
      size_t cursor = start;
      while (cursor < end) {
        const size_t split = findTopLevelTypeToken(cursor, end, ast::SyntaxKind::Ampersand);
        const size_t segmentEnd = split < end ? split : end;
        if (cursor < segmentEnd) {
          addNodeIfPresent(alts, parseTypeRange(builder, cursor, segmentEnd));
        }
        cursor = split < end ? split + 1 : end;
      }
      const ast::NodeList list = builder.makeList(alts.asPtr());
      ast::NodePayload payload;
      writeNodeList(payload, ast::kIntersectionTypeExprAltsFirstWord,
                    ast::kIntersectionTypeExprAltsSizeWord, list);
      return builder.makeNode(ast::SyntaxKind::IntersectionTypeExpr, rangeFor(start, end), payload);
    }

    if (kindAt(end - 1) == ast::SyntaxKind::Question && end > start + 1) {
      const ast::NodeId inner = parseTypeRange(builder, start, end - 1);
      if (!inner) {
        diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(
            tokenAt(end - 1).getLocation());
        return ast::NodeId();
      }

      ast::NodePayload payload;
      writeNode(payload, ast::kOptionalTypeExprInnerWord, inner);
      payload.words[ast::kOptionalTypeExprDoubleWord] = 0;
      return builder.makeNode(ast::SyntaxKind::OptionalTypeExpr, rangeFor(start, end), payload);
    }

    if (kindAt(end - 1) == ast::SyntaxKind::QuestionQuestion && end > start + 1) {
      const ast::NodeId inner = parseTypeRange(builder, start, end - 1);
      if (!inner) {
        diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(
            tokenAt(end - 1).getLocation());
        return ast::NodeId();
      }

      ast::NodePayload payload;
      writeNode(payload, ast::kOptionalTypeExprInnerWord, inner);
      payload.words[ast::kOptionalTypeExprDoubleWord] = 1;
      return builder.makeNode(ast::SyntaxKind::OptionalTypeExpr, rangeFor(start, end), payload);
    }

    if (rangeIsWrapped(start, end, ast::SyntaxKind::LeftBracket, ast::SyntaxKind::RightBracket)) {
      const size_t semi = findTopLevelTypeToken(start + 1, end - 1, ast::SyntaxKind::Semicolon);
      ast::NodePayload payload;
      if (semi < end - 1) {
        const ast::NodeId elem = parseTypeRange(builder, start + 1, semi);
        const ast::NodeId lenExpr = parseExpressionRange(builder, semi + 1, end - 1);
        if (!elem || !lenExpr) { return ast::NodeId(); }
        writeNode(payload, ast::kFixedArrayTypeExprElemWord, elem);
        writeNode(payload, ast::kFixedArrayTypeExprLenExprWord, lenExpr);
        return builder.makeNode(ast::SyntaxKind::FixedArrayTypeExpr, rangeFor(start, end), payload);
      }

      const ast::NodeId elem = parseTypeRange(builder, start + 1, end - 1);
      if (!elem) { return ast::NodeId(); }
      writeNode(payload, ast::kSliceArrayTypeExprElemWord, elem);
      return builder.makeNode(ast::SyntaxKind::SliceArrayTypeExpr, rangeFor(start, end), payload);
    }

    if (kindAt(end - 1) == ast::SyntaxKind::RightBracket) {
      for (size_t index = end - 1; index > start;) {
        --index;
        if (index == start) { break; }
        if (kindAt(index) == ast::SyntaxKind::LeftBracket &&
            rangeIsWrapped(index, end, ast::SyntaxKind::LeftBracket,
                           ast::SyntaxKind::RightBracket)) {
          const ast::NodeId elem = parseTypeRange(builder, start, index);
          if (!elem) { return ast::NodeId(); }

          ast::NodePayload payload;
          writeNode(payload, ast::kArrayTypeExprElemWord, elem);
          if (index + 1 < end - 1) {
            const ast::NodeId lenExpr = parseExpressionRange(builder, index + 1, end - 1);
            if (!lenExpr) { return ast::NodeId(); }
            writeNode(payload, ast::kArrayTypeExprLenExprWord, lenExpr);
          }
          return builder.makeNode(ast::SyntaxKind::ArrayTypeExpr, rangeFor(start, end), payload);
        }
      }
    }

    if (end == start + 1 && isPrimitiveTypeKeyword(kindAt(start))) {
      ast::NodePayload payload;
      payload.words[ast::kPredefinedTypeExprKindWord] = predefinedTypeCode(kindAt(start));
      return builder.makeNode(ast::SyntaxKind::PredefinedTypeExpr, rangeFor(start, end), payload);
    }

    size_t pathEnd = start;
    while (pathEnd < end) {
      if (kindAt(pathEnd) == ast::SyntaxKind::Identifier) {
        ++pathEnd;
        if (pathEnd < end && kindAt(pathEnd) == ast::SyntaxKind::Period) {
          ++pathEnd;
          continue;
        }
        break;
      }
      break;
    }
    if (pathEnd == start) { return ast::NodeId(); }

    zc::Vector<ast::NodeId> args;
    if (pathEnd < end && kindAt(pathEnd) == ast::SyntaxKind::LessThan &&
        kindAt(end - 1) == ast::SyntaxKind::GreaterThan) {
      size_t cursor = pathEnd + 1;
      while (cursor + 1 < end) {
        const size_t comma = findTopLevelTypeCommaOrEnd(cursor, end - 1);
        if (cursor < comma) { addNodeIfPresent(args, parseTypeRange(builder, cursor, comma)); }
        cursor = comma < end - 1 ? comma + 1 : end - 1;
      }
    }

    ast::NodePayload payload;
    writeNode(payload, ast::kNamedTypeExprPathWord, makeModulePath(builder, start, pathEnd));
    writeNodeList(payload, ast::kNamedTypeExprArgsFirstWord, ast::kNamedTypeExprArgsSizeWord,
                  builder.makeList(args.asPtr()));
    return builder.makeNode(ast::SyntaxKind::NamedTypeExpr, rangeFor(start, end), payload);
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

  ast::NodeId parseExpressionList(ast::TreeBuilder& builder, size_t start, size_t end,
                                  ast::SyntaxKind containerKind) const {
    zc::Vector<ast::NodeId> expressions;
    size_t cursor = start;
    while (cursor < end) {
      const size_t comma = findTopLevelCommaOrEnd(cursor, end);
      if (cursor < comma) {
        const size_t rest = findTopLevelToken(cursor, comma, ast::SyntaxKind::DotDotDot);
        if (rest < comma) {
          diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
              tokenAt(rest).getLocation());
        } else {
          addNodeIfPresent(expressions, parseExpressionRange(builder, cursor, comma));
        }
      }
      cursor = comma < end ? comma + 1 : end;
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
    size_t cursor = start;
    while (cursor < end) {
      const size_t comma = findTopLevelCommaOrEnd(cursor, end);
      if (cursor >= comma) { return ast::NodeId(); }
      const ast::NodeId expr = parseExpressionRange(builder, cursor, comma);
      if (!expr) { return ast::NodeId(); }
      expressions.add(expr);
      cursor = comma < end ? comma + 1 : end;
    }
    ast::NodePayload payload;
    writeNodeList(payload, ast::kCommaExprElemsFirstWord, ast::kCommaExprElemsSizeWord,
                  builder.makeList(expressions.asPtr()));
    return builder.makeNode(ast::SyntaxKind::CommaExpr, rangeFor(start, end), payload);
  }

  ast::NodeList parseExpressionArguments(ast::TreeBuilder& builder, size_t start,
                                         size_t end) const {
    zc::Vector<ast::NodeId> args;
    size_t cursor = start;
    while (cursor < end) {
      const size_t comma = findTopLevelCommaOrEnd(cursor, end);
      if (cursor < comma) { addNodeIfPresent(args, parseExpressionRange(builder, cursor, comma)); }
      cursor = comma < end ? comma + 1 : end;
    }
    return builder.makeList(args.asPtr());
  }

  ast::NodeList parseTypeArguments(ast::TreeBuilder& builder, size_t start, size_t end) const {
    zc::Vector<ast::NodeId> args;
    size_t cursor = start;
    while (cursor < end) {
      const size_t comma = findTopLevelTypeCommaOrEnd(cursor, end);
      if (cursor < comma) { addNodeIfPresent(args, parseTypeRange(builder, cursor, comma)); }
      cursor = comma < end ? comma + 1 : end;
    }
    return builder.makeList(args.asPtr());
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

  ast::NodeId parseCastExpression(ast::TreeBuilder& builder, size_t start, size_t asIndex,
                                  size_t end) const {
    size_t typeStart = asIndex + 1;
    uint8_t mode = castModeCode(ast::SyntaxKind::AsKeyword);
    if (typeStart < end && (kindAt(typeStart) == ast::SyntaxKind::Question ||
                            kindAt(typeStart) == ast::SyntaxKind::Exclamation)) {
      mode = castModeCode(kindAt(typeStart));
      ++typeStart;
    }

    const ast::NodeId expr = parseExpressionRange(builder, start, asIndex);
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
    size_t cursor = start;
    while (cursor < end) {
      while (cursor < end && kindAt(cursor) == ast::SyntaxKind::Comma) { ++cursor; }
      if (cursor >= end) { break; }

      const size_t itemEnd = findTopLevelCommaOrEnd(cursor, end);
      addNodeIfPresent(captures, parseCaptureItem(builder, cursor, itemEnd));
      cursor = itemEnd < end ? itemEnd + 1 : itemEnd;
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

  ast::NodeId parseObjectLiteralExpression(ast::TreeBuilder& builder, size_t start,
                                           size_t end) const {
    zc::Vector<ast::NodeId> properties;
    size_t cursor = start + 1;
    while (cursor + 1 < end) {
      const size_t comma = findTopLevelCommaOrEnd(cursor, end - 1);
      if (cursor < comma) {
        ast::NodePayload propertyPayload;
        if (kindAt(cursor) == ast::SyntaxKind::DotDotDot) {
          writeNode(propertyPayload, ast::kObjectSpreadExprWord,
                    parseExpressionRange(builder, cursor + 1, comma));
          properties.add(builder.makeNode(ast::SyntaxKind::ObjectSpread, rangeFor(cursor, comma),
                                          propertyPayload));
        } else {
          const size_t colon = findTopLevelToken(cursor, comma, ast::SyntaxKind::Colon);
          writeIdent(propertyPayload, ast::kObjectPropertyNameWord, internIdent(builder, cursor));
          if (colon < comma) {
            writeNode(propertyPayload, ast::kObjectPropertyValueWord,
                      parseExpressionRange(builder, colon + 1, comma));
          } else {
            propertyPayload.words[ast::kObjectPropertyShortFormWord] = 1;
          }
          properties.add(builder.makeNode(ast::SyntaxKind::ObjectProperty, rangeFor(cursor, comma),
                                          propertyPayload));
        }
      }
      cursor = comma < end - 1 ? comma + 1 : end - 1;
    }

    ast::NodePayload payload;
    writeNodeList(payload, ast::kObjectLiteralExprPropertiesFirstWord,
                  ast::kObjectLiteralExprPropertiesSizeWord, builder.makeList(properties.asPtr()));
    return builder.makeNode(ast::SyntaxKind::ObjectLiteralExpr, rangeFor(start, end), payload);
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

  ast::NodeId parseExpressionRange(ast::TreeBuilder& builder, size_t start, size_t end) const {
    while (start < end && kindAt(end - 1) == ast::SyntaxKind::Semicolon) { --end; }
    if (start >= end) { return ast::NodeId(); }

    const ast::NodeId lambda = parseLambdaExpression(builder, start, end);
    if (lambda) { return lambda; }

    if (kindAt(start) == ast::SyntaxKind::FunKeyword) {
      return parseFunctionExpression(builder, start, end);
    }

    if (rangeIsWrapped(start, end, ast::SyntaxKind::LeftParen, ast::SyntaxKind::RightParen)) {
      if (start + 1 == end - 1) {
        zc::Vector<ast::NodeId> elems;
        ast::NodePayload payload;
        writeNodeList(payload, ast::kTupleLiteralElemsFirstWord, ast::kTupleLiteralElemsSizeWord,
                      builder.makeList(elems.asPtr()));
        return builder.makeNode(ast::SyntaxKind::TupleLiteral, rangeFor(start, end), payload);
      }
      if (findTopLevelToken(start + 1, end - 1, ast::SyntaxKind::Comma) < end - 1) {
        return parseExpressionList(builder, start + 1, end - 1, ast::SyntaxKind::TupleLiteral);
      }
      return parseExpressionRange(builder, start + 1, end - 1);
    }

    if (rangeIsWrapped(start, end, ast::SyntaxKind::LeftBracket, ast::SyntaxKind::RightBracket)) {
      return parseExpressionList(builder, start + 1, end - 1, ast::SyntaxKind::ArrayLiteral);
    }

    if (rangeIsWrapped(start, end, ast::SyntaxKind::LeftBrace, ast::SyntaxKind::RightBrace)) {
      return parseObjectLiteralExpression(builder, start, end);
    }

    if (kindAt(start) == ast::SyntaxKind::TemplateHead &&
        kindAt(end - 1) == ast::SyntaxKind::TemplateTail) {
      return parseTemplateLiteralExpression(builder, start, end);
    }

    const size_t assignmentOperator = findTopLevelAssignmentOperator(start, end);
    if (assignmentOperator < end && assignmentOperator > start && assignmentOperator + 1 < end) {
      const ast::NodeId lhs = parseExpressionRange(builder, start, assignmentOperator);
      const ast::NodeId rhs = parseExpressionRange(builder, assignmentOperator + 1, end);
      if (!lhs || !rhs) { return ast::NodeId(); }
      ast::NodePayload payload;
      payload.words[ast::kAssignmentExprOpWord] = assignmentOpCode(kindAt(assignmentOperator));
      writeNode(payload, ast::kAssignmentExprLhsWord, lhs);
      writeNode(payload, ast::kAssignmentExprRhsWord, rhs);
      return builder.makeNode(ast::SyntaxKind::AssignmentExpr, rangeFor(start, end), payload);
    }

    const size_t conditionalQuestion = findTopLevelToken(start, end, ast::SyntaxKind::Question);
    if (conditionalQuestion < end) {
      const size_t conditionalColon = findTopLevelConditionalColon(conditionalQuestion, end);
      if (conditionalColon < end && conditionalQuestion > start &&
          conditionalQuestion + 1 < conditionalColon && conditionalColon + 1 < end) {
        const ast::NodeId cond = parseExpressionRange(builder, start, conditionalQuestion);
        const ast::NodeId thenExpr =
            parseExpressionRange(builder, conditionalQuestion + 1, conditionalColon);
        const ast::NodeId elseExpr = parseExpressionRange(builder, conditionalColon + 1, end);
        if (!cond || !thenExpr || !elseExpr) { return ast::NodeId(); }
        ast::NodePayload payload;
        writeNode(payload, ast::kConditionalExprCondWord, cond);
        writeNode(payload, ast::kConditionalExprThenExprWord, thenExpr);
        writeNode(payload, ast::kConditionalExprElseExprWord, elseExpr);
        return builder.makeNode(ast::SyntaxKind::ConditionalExpr, rangeFor(start, end), payload);
      }
    }

    const size_t completeCallOpen = findTrailingCallOpen(start, end);
    if (kindAt(start) == ast::SyntaxKind::NewKeyword && completeCallOpen < end) {
      return parseNewExpression(builder, start, end);
    }

    if (completeCallOpen < end && kindAt(start) != ast::SyntaxKind::TypeOfKeyword) {
      const size_t typeArgsOpen = findTrailingTypeArgumentOpen(start, completeCallOpen);
      if (typeArgsOpen < completeCallOpen || canUseRangeAsCallCallee(start, completeCallOpen)) {
        return parseCallExpression(builder, start, completeCallOpen, end);
      }
    }

    if (findTopLevelToken(start, end, ast::SyntaxKind::Comma) < end) {
      return parseCommaExpression(builder, start, end);
    }

    const size_t errorDefault = findTopLevelToken(start, end, ast::SyntaxKind::ErrorDefault);
    if (errorDefault < end) {
      const ast::NodeId primary = parseExpressionRange(builder, start, errorDefault);
      const ast::NodeId fallback = parseExpressionRange(builder, errorDefault + 1, end);
      if (!primary || !fallback) { return ast::NodeId(); }
      ast::NodePayload payload;
      writeNode(payload, ast::kErrorDefaultExprPrimaryWord, primary);
      writeNode(payload, ast::kErrorDefaultExprFallbackWord, fallback);
      return builder.makeNode(ast::SyntaxKind::ErrorDefaultExpr, rangeFor(start, end), payload);
    }

    const size_t nullCoalesce = findTopLevelToken(start, end, ast::SyntaxKind::QuestionQuestion);
    if (nullCoalesce < end) {
      const ast::NodeId primary = parseExpressionRange(builder, start, nullCoalesce);
      const ast::NodeId fallback = parseExpressionRange(builder, nullCoalesce + 1, end);
      if (!primary || !fallback) { return ast::NodeId(); }
      ast::NodePayload payload;
      writeNode(payload, ast::kNullCoalesceExprPrimaryWord, primary);
      writeNode(payload, ast::kNullCoalesceExprFallbackWord, fallback);
      return builder.makeNode(ast::SyntaxKind::NullCoalesceExpr, rangeFor(start, end), payload);
    }

    const size_t castOperator = findTopLevelToken(start, end, ast::SyntaxKind::AsKeyword);
    if (castOperator < end && castOperator > start && castOperator + 1 < end) {
      return parseCastExpression(builder, start, castOperator, end);
    }

    const size_t binaryOperator = findTopLevelBinaryOperator(start, end);
    if (binaryOperator < end && binaryOperator > start && binaryOperator + 1 < end) {
      const ast::NodeId lhs = parseExpressionRange(builder, start, binaryOperator);
      if (!lhs) { return ast::NodeId(); }
      if (kindAt(binaryOperator) == ast::SyntaxKind::IsKeyword) {
        const ast::NodeId ty = parseTypeRange(builder, binaryOperator + 1, end);
        if (!ty) { return ast::NodeId(); }
        ast::NodePayload payload;
        writeNode(payload, ast::kIsExpressionExprWord, lhs);
        writeNode(payload, ast::kIsExpressionTyWord, ty);
        return builder.makeNode(ast::SyntaxKind::IsExpression, rangeFor(start, end), payload);
      }
      const ast::NodeId rhs = parseExpressionRange(builder, binaryOperator + 1, end);
      if (!rhs) { return ast::NodeId(); }
      ast::NodePayload payload;
      payload.words[ast::kBinaryExprOpWord] = binaryOpCode(kindAt(binaryOperator));
      writeNode(payload, ast::kBinaryExprLhsWord, lhs);
      writeNode(payload, ast::kBinaryExprRhsWord, rhs);
      return builder.makeNode(ast::SyntaxKind::BinaryExpr, rangeFor(start, end), payload);
    }

    if (kindAt(start) == ast::SyntaxKind::TypeOfKeyword && start + 1 < end) {
      const ast::NodeId expr = parseExpressionRange(builder, start + 1, end);
      if (!expr) { return ast::NodeId(); }
      ast::NodePayload payload;
      writeNode(payload, ast::kTypeOfExpressionExprWord, expr);
      return builder.makeNode(ast::SyntaxKind::TypeOfExpression, rangeFor(start, end), payload);
    }

    if (isPrefixUnaryOperator(kindAt(start)) && start + 1 < end) {
      const ast::NodeId operand = parseExpressionRange(builder, start + 1, end);
      if (!operand) { return ast::NodeId(); }
      ast::NodePayload payload;
      payload.words[ast::kUnaryExpressionOpWord] = unaryOpCode(kindAt(start));
      writeNode(payload, ast::kUnaryExpressionOperandWord, operand);
      return builder.makeNode(ast::SyntaxKind::UnaryExpression, rangeFor(start, end), payload);
    }

    if (isPostfixOperator(kindAt(end - 1)) && start + 1 < end) {
      const ast::NodeId operand = parseExpressionRange(builder, start, end - 1);
      if (!operand) { return ast::NodeId(); }
      ast::NodePayload payload;
      payload.words[ast::kPostfixExpressionOpWord] = postfixOpCode(kindAt(end - 1));
      writeNode(payload, ast::kPostfixExpressionOperandWord, operand);
      return builder.makeNode(ast::SyntaxKind::PostfixExpression, rangeFor(start, end), payload);
    }

    const size_t indexOpen = findTrailingIndexOpen(start, end);
    if (indexOpen < end && indexOpen > start) {
      const size_t objectEnd =
          kindAt(indexOpen - 1) == ast::SyntaxKind::QuestionDot ? indexOpen - 1 : indexOpen;
      const ast::NodeId object = parseExpressionRange(builder, start, objectEnd);
      const ast::NodeId index = parseExpressionRange(builder, indexOpen + 1, end - 1);
      if (!object || !index) { return ast::NodeId(); }
      ast::NodePayload payload;
      writeNode(payload, ast::kIndexExpressionObjectWord, object);
      writeNode(payload, ast::kIndexExpressionIndexWord, index);
      return builder.makeNode(ast::SyntaxKind::IndexExpression, rangeFor(start, end), payload);
    }

    const size_t memberOperator = findTrailingMemberOperator(start, end);
    if (memberOperator < end) {
      const ast::NodeId object = parseExpressionRange(builder, start, memberOperator);
      if (!object) { return ast::NodeId(); }
      ast::NodePayload payload;
      writeNode(payload, ast::kMemberExpressionObjectWord, object);
      writeIdent(payload, ast::kMemberExpressionPropertyWord,
                 internIdent(builder, memberOperator + 1));
      return builder.makeNode(ast::SyntaxKind::MemberExpression, rangeFor(start, end), payload);
    }

    if (kindAt(start) == ast::SyntaxKind::NewKeyword) {
      return parseNewExpression(builder, start, end);
    }

    if (kindAt(start) == ast::SyntaxKind::ImportKeyword) {
      return parseImportCallExpression(builder, start, end);
    }

    if (kindAt(start) == ast::SyntaxKind::TemplateHead) {
      return parseTemplateLiteralExpression(builder, start, end);
    }

    const size_t callOpen = findTrailingCallOpen(start, end);
    if (callOpen < end) {
      const size_t typeArgsOpen = findTrailingTypeArgumentOpen(start, callOpen);
      if (typeArgsOpen < callOpen || canUseRangeAsCallCallee(start, callOpen)) {
        return parseCallExpression(builder, start, callOpen, end);
      }
    }

    if (end == start + 1) {
      ast::NodePayload payload;
      switch (kindAt(start)) {
        case ast::SyntaxKind::Identifier:
          if (tokenAt(start).getValue() == "_"_zc) { return ast::NodeId(); }
          writeIdent(payload, ast::kIdentExprNameWord, internIdent(builder, start));
          return builder.makeNode(ast::SyntaxKind::IdentExpr, rangeFor(start, end), payload);
        case ast::SyntaxKind::ThisKeyword:
          return builder.makeNode(ast::SyntaxKind::ThisExpr, rangeFor(start, end), payload);
        case ast::SyntaxKind::TrueKeyword:
        case ast::SyntaxKind::FalseKeyword:
          payload.words[ast::kBoolLiteralValueWord] =
              kindAt(start) == ast::SyntaxKind::TrueKeyword ? 1 : 0;
          return builder.makeNode(ast::SyntaxKind::BoolLiteral, rangeFor(start, end), payload);
        case ast::SyntaxKind::NullKeyword:
          return builder.makeNode(ast::SyntaxKind::NullLiteral, rangeFor(start, end), payload);
        case ast::SyntaxKind::UnitKeyword:
          return builder.makeNode(ast::SyntaxKind::UnitLiteral, rangeFor(start, end), payload);
        case ast::SyntaxKind::IntegerLiteral:
          payload.words[ast::kIntLiteralBaseWord] = integerBase(tokenAt(start).getValue());
          writeBigInt(payload, ast::kIntLiteralValueWord,
                      builder.internBigInt(tokenAt(start).getValue()));
          return builder.makeNode(ast::SyntaxKind::IntLiteral, rangeFor(start, end), payload);
        case ast::SyntaxKind::BigIntLiteral:
          writeBigInt(payload, ast::kBigIntLiteralValueWord,
                      builder.internBigInt(tokenAt(start).getValue()));
          return builder.makeNode(ast::SyntaxKind::BigIntLiteral, rangeFor(start, end), payload);
        case ast::SyntaxKind::FloatLiteral:
          payload.words[ast::kFloatLiteralExprWidthWord] = 64;
          writeFloat(payload, ast::kFloatLiteralExprValueWord,
                     builder.internFloat(tokenAt(start).getValue()));
          return builder.makeNode(ast::SyntaxKind::FloatLiteralExpr, rangeFor(start, end), payload);
        case ast::SyntaxKind::StringLiteral:
        case ast::SyntaxKind::NoSubstitutionTemplateLiteral:
          writeString(payload, ast::kStrLiteralValueWord, internString(builder, start));
          return builder.makeNode(ast::SyntaxKind::StrLiteral, rangeFor(start, end), payload);
        default:
          break;
      }
    }

    if (end == start + 1 && isExpressionIdentifierLike(kindAt(start))) {
      ast::NodePayload payload;
      writeIdent(payload, ast::kIdentExprNameWord, internIdent(builder, start));
      return builder.makeNode(ast::SyntaxKind::IdentExpr, rangeFor(start, start + 1), payload);
    }
    return ast::NodeId();
  }

  ast::NodeId parsePatternRange(ast::TreeBuilder& builder, size_t start, size_t end) const {
    if (start >= end) { return ast::NodeId(); }

    ast::NodePayload payload;
    const size_t at = findTopLevelToken(start, end, ast::SyntaxKind::At);
    if (at < end) {
      diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
          tokenAt(at).getLocation());
      return ast::NodeId();
    }

    if (rangeIsWrapped(start, end, ast::SyntaxKind::LeftBrace, ast::SyntaxKind::RightBrace)) {
      zc::Vector<ast::NodeId> properties;
      ast::NodeId rest;
      size_t cursor = start + 1;
      while (cursor + 1 < end) {
        const size_t comma = findTopLevelCommaOrEnd(cursor, end - 1);
        if (cursor < comma) {
          if (kindAt(cursor) == ast::SyntaxKind::DotDotDot) {
            ast::NodePayload restPayload;
            if (cursor + 1 < comma && kindAt(cursor + 1) == ast::SyntaxKind::Identifier) {
              writeIdent(restPayload, ast::kRestPatternBindingWord,
                         internIdent(builder, cursor + 1));
            }
            rest = builder.makeNode(ast::SyntaxKind::RestPattern, rangeFor(cursor, comma),
                                    restPayload);
          } else {
            const size_t colon = findTopLevelToken(cursor, comma, ast::SyntaxKind::Colon);
            ast::NodePayload propertyPayload;
            writeIdent(propertyPayload, ast::kPatternPropertyNameWord,
                       internIdent(builder, cursor));
            if (colon < comma) {
              writeNode(propertyPayload, ast::kPatternPropertyPatWord,
                        parsePatternRange(builder, colon + 1, comma));
            } else {
              propertyPayload.words[ast::kPatternPropertyShortFormWord] = 1;
            }
            properties.add(builder.makeNode(ast::SyntaxKind::PatternProperty,
                                            rangeFor(cursor, comma), propertyPayload));
          }
        }
        cursor = comma < end - 1 ? comma + 1 : end - 1;
      }

      writeNodeList(payload, ast::kStructPatternFieldsFirstWord, ast::kStructPatternFieldsSizeWord,
                    builder.makeList(properties.asPtr()));
      writeNode(payload, ast::kStructPatternRestWord, rest);
      return builder.makeNode(ast::SyntaxKind::StructPattern, rangeFor(start, end), payload);
    }

    if (end == start + 1 && kindAt(start) == ast::SyntaxKind::Identifier) {
      writeIdent(payload, ast::kIdentifierPatternNameWord, internIdent(builder, start));
      return builder.makeNode(ast::SyntaxKind::IdentifierPattern, rangeFor(start, end), payload);
    }

    return builder.makeNode(ast::SyntaxKind::WildcardPattern, rangeFor(start, end), payload);
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

  ast::NodeId parseFunctionParameterList(ast::TreeBuilder& builder, size_t openParen,
                                         size_t closeParen) const {
    zc::Vector<ast::NodeId> parameters;
    if (openParen < closeParen && closeParen <= tokenCountWithoutEof()) {
      size_t cursor = openParen + 1;
      while (cursor < closeParen) {
        const size_t comma = findTopLevelCommaOrEnd(cursor, closeParen);
        if (cursor < comma) {
          size_t parameterStart = cursor;
          if (isOuterAttributeStart(parameterStart, comma)) {
            diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
                tokenAt(parameterStart).getLocation());
            parameterStart = skipOuterAttributePrefix(parameterStart, comma);
          }

          if (parameterStart >= comma) {
            cursor = comma < closeParen ? comma + 1 : closeParen;
            continue;
          }

          const size_t colon = findTopLevelToken(parameterStart, comma, ast::SyntaxKind::Colon);
          if (colon >= comma) {
            diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(
                diagnosticLoc(parameterStart + 1), ":"_zc);
            cursor = comma < closeParen ? comma + 1 : closeParen;
            continue;
          }

          const ast::IdentId name = kindAt(parameterStart) == ast::SyntaxKind::Identifier
                                        ? internIdent(builder, parameterStart)
                                        : ast::IdentId();
          if (!name) {
            diagnosticEngine.diagnose<diagnostics::DiagID::IdentifierExpected>(
                tokenAt(parameterStart).getLocation());
            cursor = comma < closeParen ? comma + 1 : closeParen;
            continue;
          }

          ast::NodePayload parameterPayload;
          writeIdent(parameterPayload, ast::kFunctionParameterDeclNameWord, name);
          const size_t defaultEquals = findTopLevelToken(colon + 1, comma, ast::SyntaxKind::Equals);
          const size_t typeEnd = defaultEquals < comma ? defaultEquals : comma;
          const ast::NodeId ty = parseTypeRange(builder, colon + 1, typeEnd);
          if (!ty) {
            diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(colon + 1));
            cursor = comma < closeParen ? comma + 1 : closeParen;
            continue;
          }

          writeNode(parameterPayload, ast::kFunctionParameterDeclTyWord, ty);
          if (defaultEquals < comma) {
            writeNode(parameterPayload, ast::kFunctionParameterDeclDefaultWord,
                      parseRequiredExpression(builder, defaultEquals + 1, comma));
          }
          parameters.add(builder.makeNode(ast::SyntaxKind::FunctionParameterDecl,
                                          rangeFor(parameterStart, comma), parameterPayload));
        }
        cursor = comma < closeParen ? comma + 1 : closeParen;
      }
    }

    const ast::NodeList parameterList = builder.makeList(parameters.asPtr());
    ast::NodePayload payload;
    payload.words[ast::kFunctionParameterListNparamsWord] =
        static_cast<uint32_t>(parameters.size());
    writeNodeList(payload, ast::kFunctionParameterListParamsFirstWord,
                  ast::kFunctionParameterListParamsSizeWord, parameterList);
    return builder.makeNode(ast::SyntaxKind::FunctionParameterList,
                            rangeFor(openParen, closeParen + 1), payload);
  }

  ast::NodeId parseBlock(ast::TreeBuilder& builder, size_t openBrace, size_t limit) const {
    zc::Vector<ast::NodeId> items;
    if (openBrace >= limit || kindAt(openBrace) != ast::SyntaxKind::LeftBrace) {
      ast::NodePayload payload;
      writeNodeList(payload, ast::kBlockStmtStmtsFirstWord, ast::kBlockStmtStmtsSizeWord,
                    builder.makeList(items.asPtr()));
      return builder.makeNode(ast::SyntaxKind::BlockStmt, rangeFor(openBrace, openBrace), payload);
    }

    const size_t closeBrace = findMatchingRightBrace(openBrace, limit);
    const size_t bodyEnd = closeBrace < limit ? closeBrace : limit;
    size_t cursor = openBrace + 1;
    while (cursor < bodyEnd) {
      const size_t statementEnd = findStatementEndBefore(cursor, bodyEnd);
      const ast::SyntaxKind itemKind = classifyStatement(cursor, statementEnd);
      if (outerAttributePrefixContainsZomCfg(cursor, statementEnd) &&
          itemKind != ast::SyntaxKind::BlockStmt) {
        diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(
            tokenAt(effectiveStatementStart(cursor, statementEnd)).getLocation(),
            "cfg-gated block"_zc);
      }
      const ast::NodeId attrs = parseOuterAttributeList(builder, cursor, statementEnd);
      const ast::NodeId item = parseSourceElement(builder, cursor, statementEnd);
      if (item) {
        items.add(makeStatementListItem(builder, item, rangeFor(cursor, statementEnd), attrs));
      }
      cursor = statementEnd > cursor ? statementEnd : cursor + 1;
    }

    ast::NodePayload payload;
    writeNodeList(payload, ast::kBlockStmtStmtsFirstWord, ast::kBlockStmtStmtsSizeWord,
                  builder.makeList(items.asPtr()));
    return builder.makeNode(ast::SyntaxKind::BlockStmt, rangeFor(openBrace, bodyEnd + 1), payload);
  }

  ast::NodeId parseStatementBody(ast::TreeBuilder& builder, size_t start, size_t end) const {
    if (start >= end) { return parseBlock(builder, end, end); }
    if (kindAt(start) == ast::SyntaxKind::LeftBrace) { return parseBlock(builder, start, end); }
    return parseSourceElement(builder, start, end);
  }

  ast::NodeId parseModuleDeclaration(ast::TreeBuilder& builder, size_t start, size_t end) const {
    const size_t pathEnd = findTopLevelToken(start + 1, end, ast::SyntaxKind::Semicolon);
    ast::NodePayload payload;
    writeNode(payload, ast::kModuleDeclarationPathWord,
              makeModulePath(builder, start + 1, pathEnd < end ? pathEnd : end));
    return builder.makeNode(ast::SyntaxKind::ModuleDeclaration, rangeFor(start, end), payload);
  }

  ast::NodeId parseImportDeclaration(ast::TreeBuilder& builder, size_t start, size_t end) const {
    const size_t pathEnd = findTopLevelToken(start + 1, end, ast::SyntaxKind::Semicolon);
    zc::Vector<ast::NodeId> specifiers;
    ast::NodePayload payload;
    writeNode(payload, ast::kImportDeclarationPathWord,
              makeModulePath(builder, start + 1, pathEnd < end ? pathEnd : end));
    writeNodeList(payload, ast::kImportDeclarationSpecifiersFirstWord,
                  ast::kImportDeclarationSpecifiersSizeWord, builder.makeList(specifiers.asPtr()));
    return builder.makeNode(ast::SyntaxKind::ImportDeclaration, rangeFor(start, end), payload);
  }

  ast::NodeId parseExportDeclaration(ast::TreeBuilder& builder, size_t start, size_t end) const {
    zc::Vector<ast::NodeId> specifiers;
    ast::NodePayload payload;
    if (start + 1 < end && kindAt(start + 1) != ast::SyntaxKind::LeftBrace &&
        kindAt(start + 1) != ast::SyntaxKind::DefaultKeyword) {
      writeNode(payload, ast::kExportDeclarationDeclarationWord,
                parseSourceElement(builder, start + 1, end));
    }
    writeNodeList(payload, ast::kExportDeclarationSpecifiersFirstWord,
                  ast::kExportDeclarationSpecifiersSizeWord, builder.makeList(specifiers.asPtr()));
    return builder.makeNode(ast::SyntaxKind::ExportDeclaration, rangeFor(start, end), payload);
  }

  ast::NodeId parseLetStatement(ast::TreeBuilder& builder, size_t start, size_t end) const {
    size_t patternStart = start + 1;
    if (patternStart < end && kindAt(patternStart) == ast::SyntaxKind::Identifier &&
        tokenAt(patternStart).getValue() == "mut"_zc) {
      ++patternStart;
    }

    const size_t equals = findTopLevelToken(patternStart, end, ast::SyntaxKind::Equals);
    const size_t colon =
        findTopLevelToken(patternStart, equals < end ? equals : end, ast::SyntaxKind::Colon);
    const size_t patternEnd = colon < end ? colon : (equals < end ? equals : end);
    if (equals >= end) {
      diagnosticEngine.diagnose<diagnostics::DiagID::VariableDeclarationExpected>(
          diagnosticLoc(patternEnd));
      return ast::NodeId();
    }

    ast::NodePayload payload;
    writeNode(payload, ast::kLetStmtPatternWord,
              parsePatternRange(builder, patternStart, patternEnd));
    if (colon < end) {
      writeNode(payload, ast::kLetStmtTyWord,
                parseTypeRange(builder, colon + 1, equals < end ? equals : end));
    }
    if (equals < end) {
      writeNode(payload, ast::kLetStmtInitWord, parseRequiredExpression(builder, equals + 1, end));
    }
    return builder.makeNode(ast::SyntaxKind::LetStmt, rangeFor(start, end), payload);
  }

  ast::NodeId parseReturnStatement(ast::TreeBuilder& builder, size_t start, size_t end) const {
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
    size_t condStart = start + 1;
    size_t condEnd = end;
    size_t thenStart = end;
    conditionRangeAfterKeyword(start, end, condStart, condEnd, thenStart);

    const size_t elseIndex = findTopLevelToken(thenStart, end, ast::SyntaxKind::ElseKeyword);
    const size_t thenEnd = elseIndex < end ? elseIndex : end;

    ast::NodePayload payload;
    writeNode(payload, ast::kIfStmtCondWord, parseExpressionRange(builder, condStart, condEnd));
    writeNode(payload, ast::kIfStmtThenStmtWord, parseStatementBody(builder, thenStart, thenEnd));
    if (elseIndex < end) {
      writeNode(payload, ast::kIfStmtElseStmtWord, parseStatementBody(builder, elseIndex + 1, end));
    }
    return builder.makeNode(ast::SyntaxKind::IfStmt, rangeFor(start, end), payload);
  }

  ast::NodeId parseWhileStatement(ast::TreeBuilder& builder, size_t start, size_t end) const {
    size_t condStart = start + 1;
    size_t condEnd = end;
    size_t bodyStart = end;
    conditionRangeAfterKeyword(start, end, condStart, condEnd, bodyStart);

    ast::NodePayload payload;
    writeNode(payload, ast::kWhileStmtCondWord, parseExpressionRange(builder, condStart, condEnd));
    writeNode(payload, ast::kWhileStmtBodyWord, parseStatementBody(builder, bodyStart, end));
    return builder.makeNode(ast::SyntaxKind::WhileStmt, rangeFor(start, end), payload);
  }

  ast::NodeId parseDoWhileStatement(ast::TreeBuilder& builder, size_t start, size_t end) const {
    const size_t whileIndex = findTopLevelToken(start + 1, end, ast::SyntaxKind::WhileKeyword);
    ast::NodePayload payload;
    writeNode(payload, ast::kDoWhileStatementBodyWord,
              parseStatementBody(builder, start + 1, whileIndex < end ? whileIndex : end));

    if (whileIndex < end) {
      size_t condStart = whileIndex + 1;
      size_t condEnd = end;
      size_t ignoredBodyStart = end;
      conditionRangeAfterKeyword(whileIndex, end, condStart, condEnd, ignoredBodyStart);
      writeNode(payload, ast::kDoWhileStatementCondWord,
                parseExpressionRange(builder, condStart, condEnd));
    }
    return builder.makeNode(ast::SyntaxKind::DoWhileStatement, rangeFor(start, end), payload);
  }

  ast::NodeId parseBreakStatement(ast::TreeBuilder& builder, size_t start, size_t end) const {
    ast::NodePayload payload;
    if (start + 1 < end && kindAt(start + 1) == ast::SyntaxKind::Identifier) {
      writeIdent(payload, ast::kBreakStmtLabelWord, internIdent(builder, start + 1));
    }
    return builder.makeNode(ast::SyntaxKind::BreakStmt, rangeFor(start, end), payload);
  }

  ast::NodeId parseContinueStatement(ast::TreeBuilder& builder, size_t start, size_t end) const {
    ast::NodePayload payload;
    if (start + 1 < end && kindAt(start + 1) == ast::SyntaxKind::Identifier) {
      writeIdent(payload, ast::kContinueStatementLabelWord, internIdent(builder, start + 1));
    }
    return builder.makeNode(ast::SyntaxKind::ContinueStatement, rangeFor(start, end), payload);
  }

  ast::NodeId parseLabeledStatement(ast::TreeBuilder& builder, size_t start, size_t end) const {
    ast::NodePayload payload;
    writeIdent(payload, ast::kLabeledStatementLabelWord, internIdent(builder, start));
    if (start + 2 < end) {
      writeNode(payload, ast::kLabeledStatementStatementWord,
                parseStatementBody(builder, start + 2, end));
    }
    return builder.makeNode(ast::SyntaxKind::LabeledStatement, rangeFor(start, end), payload);
  }

  ast::NodeId parseForStatement(ast::TreeBuilder& builder, size_t start, size_t end) const {
    const size_t openParen =
        start + 1 < end && kindAt(start + 1) == ast::SyntaxKind::LeftParen ? start + 1 : end;
    const size_t closeParen = findMatchingRightParen(openParen, end);
    const size_t headerStart = openParen < end ? openParen + 1 : start + 1;
    const size_t headerEnd = closeParen < end ? closeParen : end;
    const size_t bodyStart = closeParen < end ? closeParen + 1 : end;
    const size_t firstSemi = findTopLevelToken(headerStart, headerEnd, ast::SyntaxKind::Semicolon);
    const size_t secondSemi = firstSemi < headerEnd ? findTopLevelToken(firstSemi + 1, headerEnd,
                                                                        ast::SyntaxKind::Semicolon)
                                                    : headerEnd;

    ast::NodePayload payload;
    if (headerStart < firstSemi) {
      writeNode(payload, ast::kForStmtInitWord,
                parseSourceElement(builder, headerStart, firstSemi + 1));
    }
    if (firstSemi < headerEnd && firstSemi + 1 < secondSemi) {
      writeNode(payload, ast::kForStmtCondWord,
                parseExpressionRange(builder, firstSemi + 1, secondSemi));
    }
    if (secondSemi < headerEnd && secondSemi + 1 < headerEnd) {
      writeNode(payload, ast::kForStmtUpdateWord,
                parseExpressionRange(builder, secondSemi + 1, headerEnd));
    }
    writeNode(payload, ast::kForStmtBodyWord, parseStatementBody(builder, bodyStart, end));
    return builder.makeNode(ast::SyntaxKind::ForStmt, rangeFor(start, end), payload);
  }

  ast::NodeId parseForInStatement(ast::TreeBuilder& builder, size_t start, size_t end) const {
    const size_t openParen =
        start + 1 < end && kindAt(start + 1) == ast::SyntaxKind::LeftParen ? start + 1 : end;
    const size_t closeParen = findMatchingRightParen(openParen, end);
    const size_t headerStart = openParen < end ? openParen + 1 : start + 1;
    const size_t headerEnd = closeParen < end ? closeParen : end;
    const size_t inIndex = findTopLevelToken(headerStart, headerEnd, ast::SyntaxKind::InKeyword);
    const size_t bodyStart = closeParen < end ? closeParen + 1 : end;

    ast::NodePayload payload;
    if (headerStart < inIndex) {
      size_t bindingStart = headerStart;
      if (kindAt(bindingStart) == ast::SyntaxKind::LetKeyword ||
          kindAt(bindingStart) == ast::SyntaxKind::ConstKeyword) {
        ++bindingStart;
      }
      if (bindingStart < inIndex && kindAt(bindingStart) == ast::SyntaxKind::Identifier &&
          tokenAt(bindingStart).getValue() == "mut"_zc) {
        ++bindingStart;
      }
      writeNode(payload, ast::kForInStatementBindingWord,
                parsePatternRange(builder, bindingStart, inIndex));
    }
    if (inIndex < headerEnd && inIndex + 1 < headerEnd) {
      writeNode(payload, ast::kForInStatementExpressionWord,
                parseExpressionRange(builder, inIndex + 1, headerEnd));
    }
    writeNode(payload, ast::kForInStatementBodyWord, parseStatementBody(builder, bodyStart, end));
    return builder.makeNode(ast::SyntaxKind::ForInStatement, rangeFor(start, end), payload);
  }

  ast::NodeId parseMatchStatement(ast::TreeBuilder& builder, size_t start, size_t end) const {
    const size_t bodyOpen = findTopLevelToken(start + 1, end, ast::SyntaxKind::LeftBrace);
    const size_t scrutineeStart =
        start + 1 < end && kindAt(start + 1) == ast::SyntaxKind::LeftParen ? start + 2 : start + 1;
    size_t scrutineeEnd = bodyOpen < end ? bodyOpen : end;
    if (scrutineeStart > start + 1) {
      const size_t closeParen = findMatchingRightParen(start + 1, end);
      if (closeParen < scrutineeEnd) { scrutineeEnd = closeParen; }
    }

    zc::Vector<ast::NodeId> arms;
    ast::NodePayload payload;
    writeNode(payload, ast::kMatchStmtScrutineeWord,
              parseExpressionRange(builder, scrutineeStart, scrutineeEnd));
    writeNodeList(payload, ast::kMatchStmtArmsFirstWord, ast::kMatchStmtArmsSizeWord,
                  builder.makeList(arms.asPtr()));
    return builder.makeNode(ast::SyntaxKind::MatchStmt, rangeFor(start, end), payload);
  }

  ast::NodeId parseFunctionDeclaration(ast::TreeBuilder& builder, size_t start, size_t end) const {
    size_t nameIndex = end;
    for (size_t index = start + 1; index < end; ++index) {
      if (kindAt(index) == ast::SyntaxKind::Identifier) {
        nameIndex = index;
        break;
      }
    }

    size_t openParen = end;
    for (size_t index = nameIndex + 1; index < end; ++index) {
      if (kindAt(index) == ast::SyntaxKind::LeftParen) {
        openParen = index;
        break;
      }
    }
    size_t closeParen = openParen;
    if (openParen < end) {
      int32_t depth = 0;
      for (size_t index = openParen; index < end; ++index) {
        if (kindAt(index) == ast::SyntaxKind::LeftParen) { ++depth; }
        if (kindAt(index) == ast::SyntaxKind::RightParen) {
          --depth;
          if (depth == 0) {
            closeParen = index;
            break;
          }
        }
      }
    }

    const size_t bodyOpen = findTopLevelToken(closeParen + 1, end, ast::SyntaxKind::LeftBrace);
    const size_t arrow =
        findTopLevelToken(closeParen + 1, bodyOpen < end ? bodyOpen : end, ast::SyntaxKind::Arrow);
    const size_t raises = findTopLevelToken(closeParen + 1, bodyOpen < end ? bodyOpen : end,
                                            ast::SyntaxKind::RaisesKeyword);

    ast::NodePayload payload;
    if (nameIndex < end) {
      writeIdent(payload, ast::kFunctionDeclNameWord, internIdent(builder, nameIndex));
    }
    writeNode(payload, ast::kFunctionDeclParamsIdWord,
              parseFunctionParameterList(builder, openParen, closeParen));
    if (arrow < end) {
      const size_t retEnd = raises < end ? raises : (bodyOpen < end ? bodyOpen : end);
      writeNode(payload, ast::kFunctionDeclRetTyWord, parseTypeRange(builder, arrow + 1, retEnd));
    }
    if (raises < end) {
      writeNode(payload, ast::kFunctionDeclRaisesTyWord,
                parseTypeRange(builder, raises + 1, bodyOpen < end ? bodyOpen : end));
    }
    writeNode(payload, ast::kFunctionDeclBodyWord, parseBlock(builder, bodyOpen, end));
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
    size_t nameIndex = end;
    for (size_t index = start + 1; index < end; ++index) {
      if (kindAt(index) == ast::SyntaxKind::Identifier) {
        nameIndex = index;
        break;
      }
    }

    const size_t equals = findTopLevelToken(start + 1, end, ast::SyntaxKind::Equals);
    ast::NodePayload payload;
    if (nameIndex < end) {
      writeIdent(payload, ast::kAliasDeclNameWord, internIdent(builder, nameIndex));
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

  ast::NodeId parseExpressionStatement(ast::TreeBuilder& builder, size_t start, size_t end) const {
    ast::NodePayload payload;
    writeNode(payload, ast::kExpressionStatementExpressionWord,
              parseRequiredExpression(builder, start, end));
    return builder.makeNode(ast::SyntaxKind::ExpressionStatement, rangeFor(start, end), payload);
  }

  ast::NodeId parseSourceElement(ast::TreeBuilder& builder, size_t start, size_t end) const {
    if (start >= end) { return ast::NodeId(); }

    const size_t elementStart = skipOuterAttributePrefix(start, end);
    if (elementStart >= end) { return ast::NodeId(); }

    const ast::SyntaxKind kind = classifyStatement(elementStart, end);
    switch (kind) {
      case ast::SyntaxKind::ModuleDeclaration:
        return parseModuleDeclaration(builder, elementStart, end);
      case ast::SyntaxKind::ImportDeclaration:
        return parseImportDeclaration(builder, elementStart, end);
      case ast::SyntaxKind::ExportDeclaration:
        return parseExportDeclaration(builder, elementStart, end);
      case ast::SyntaxKind::LetStmt:
        return parseLetStatement(builder, elementStart, end);
      case ast::SyntaxKind::FunctionDecl:
        return parseFunctionDeclaration(builder, elementStart, end);
      case ast::SyntaxKind::ClassDecl:
      case ast::SyntaxKind::StructDecl:
      case ast::SyntaxKind::InterfaceDecl:
      case ast::SyntaxKind::EnumDeclaration:
        return parseNamedTypeDeclaration(builder, elementStart, end,
                                         classifyStatement(elementStart, end));
      case ast::SyntaxKind::ErrorDecl:
        return parseErrorDeclaration(builder, elementStart, end);
      case ast::SyntaxKind::AliasDecl:
        return parseAliasDeclaration(builder, elementStart, end);
      case ast::SyntaxKind::ReturnStmt:
        return parseReturnStatement(builder, elementStart, end);
      case ast::SyntaxKind::BlockStmt:
        return parseBlock(builder, elementStart, end);
      case ast::SyntaxKind::IfStmt:
        return parseIfStatement(builder, elementStart, end);
      case ast::SyntaxKind::MatchStmt:
        return parseMatchStatement(builder, elementStart, end);
      case ast::SyntaxKind::WhileStmt:
        return parseWhileStatement(builder, elementStart, end);
      case ast::SyntaxKind::ForStmt:
        return parseForStatement(builder, elementStart, end);
      case ast::SyntaxKind::ForInStatement:
        return parseForInStatement(builder, elementStart, end);
      case ast::SyntaxKind::BreakStmt:
        return parseBreakStatement(builder, elementStart, end);
      case ast::SyntaxKind::ContinueStatement:
        return parseContinueStatement(builder, elementStart, end);
      case ast::SyntaxKind::LabeledStatement:
        return parseLabeledStatement(builder, elementStart, end);
      case ast::SyntaxKind::DoWhileStatement:
        return parseDoWhileStatement(builder, elementStart, end);
      case ast::SyntaxKind::EmptyStatement:
        return builder.makeNode(ast::SyntaxKind::EmptyStatement, rangeFor(elementStart, end));
      case ast::SyntaxKind::DebuggerStatement:
        return builder.makeNode(ast::SyntaxKind::DebuggerStatement, rangeFor(elementStart, end));
      case ast::SyntaxKind::ExpressionStatement:
        return parseExpressionStatement(builder, elementStart, end);
      default:
        return parseExpressionStatement(builder, elementStart, end);
    }
  }

  bool shouldContinueAfterClosedBrace(ast::SyntaxKind first, size_t nextIndex) const {
    if (nextIndex >= tokenCountWithoutEof()) { return false; }

    const ast::SyntaxKind next = kindAt(nextIndex);
    if (next == ast::SyntaxKind::ElseKeyword) { return true; }
    if (first == ast::SyntaxKind::DoKeyword && next == ast::SyntaxKind::WhileKeyword) {
      return true;
    }
    if (canOwnBracedBody(first) && next == ast::SyntaxKind::LeftBrace) { return true; }
    return false;
  }

  bool canContinueLetInitializerBefore(size_t index) const {
    if (index == 0) { return false; }

    const ast::SyntaxKind kind = kindAt(index);
    const ast::SyntaxKind previous = kindAt(index - 1);
    if ((kind == ast::SyntaxKind::FunKeyword || kind == ast::SyntaxKind::ImportKeyword) &&
        previous == ast::SyntaxKind::Equals) {
      return true;
    }
    if (lexer::isKeyword(kind) &&
        (previous == ast::SyntaxKind::Period || previous == ast::SyntaxKind::QuestionDot)) {
      return true;
    }
    return false;
  }

  size_t findStatementEndBefore(size_t start, size_t count) const {
    int32_t parenDepth = 0;
    int32_t bracketDepth = 0;
    int32_t braceDepth = 0;
    bool sawBrace = false;
    const ast::SyntaxKind first = kindAt(effectiveStatementStart(start, count));

    for (size_t i = start; i < count; ++i) {
      const ast::SyntaxKind kind = kindAt(i);

      if (i > start && kind != ast::SyntaxKind::Semicolon && parenDepth == 0 && bracketDepth == 0 &&
          braceDepth == 0 && isTopLevelStart(kind) && first == ast::SyntaxKind::LetKeyword &&
          !canContinueLetInitializerBefore(i)) {
        diagnosticEngine.diagnose<diagnostics::DiagID::MissingSemicolon>(tokenAt(i).getLocation(),
                                                                         tokenLabel(tokenAt(i)));
        return i;
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
          sawBrace = true;
          break;
        case ast::SyntaxKind::RightBrace:
          if (braceDepth > 0) {
            --braceDepth;
            if (braceDepth == 0 && parenDepth == 0 && bracketDepth == 0 &&
                canOwnBracedBody(first) && !shouldContinueAfterClosedBrace(first, i + 1)) {
              return i + 1;
            }
          } else {
            emitUnexpected(tokenAt(i));
            return i + 1;
          }
          break;
        case ast::SyntaxKind::Semicolon:
          if (parenDepth == 0 && bracketDepth == 0 && braceDepth == 0) { return i + 1; }
          break;
        default:
          break;
      }
    }

    if (sawBrace && braceDepth > 0) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(
          tokenAt(count - 1).getLocation(), "}"_zc);
    }
    return count;
  }

  size_t findStatementEnd(size_t start) {
    return findStatementEndBefore(start, tokenCountWithoutEof());
  }

  ast::Tree buildTree() {
    ast::TreeBuilder builder;
    ast::NodeId moduleNode;
    zc::Vector<ast::NodeId> statements;
    bool firstSourceElement = true;

    size_t index = 0;
    const size_t count = tokenCountWithoutEof();
    while (index < count) {
      const size_t end = findStatementEnd(index);
      const size_t elementStart = effectiveStatementStart(index, end);
      const ast::SyntaxKind first = kindAt(elementStart);
      const ast::NodeId attrs = parseOuterAttributeList(builder, index, end);
      const ast::NodeId element = parseSourceElement(builder, index, end);
      if (outerAttributePrefixContainsZomCfg(index, end) &&
          !isTopLevelCfgAttributeTarget(classifyStatement(index, end))) {
        diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(
            tokenAt(elementStart).getLocation(), "cfg-gated declaration or block"_zc);
      }

      if (first == ast::SyntaxKind::ModuleKeyword && firstSourceElement && !moduleNode && !attrs) {
        moduleNode = element;
      } else {
        if (first == ast::SyntaxKind::ModuleKeyword) {
          diagnosticEngine.diagnose<diagnostics::DiagID::ModuleDeclarationMustBeFirst>(
              tokenAt(elementStart).getLocation());
        }
        if (element) {
          statements.add(makeStatementListItem(builder, element, rangeFor(index, end), attrs));
        }
      }

      firstSourceElement = false;
      index = end > index ? end : index + 1;
    }

    const ast::NodeList statementList = builder.makeList(statements.asPtr());
    const ast::NodeId root = builder.makeNode(
        ast::SyntaxKind::SourceFile, rangeFor(0, tokens.size()),
        makeSourceFilePayload(builder.internString(sourceMgr.getIdentifierForBuffer(bufferId)),
                              moduleNode, statementList));
    builder.setRoot(root);
    return builder.finish();
  }
};

Parser::Parser(const source::SourceManager& sourceMgr,
               diagnostics::DiagnosticEngine& diagnosticEngine, const basic::LangOptions& langOpts,
               basic::StringPool& stringPool, const source::BufferId& bufferId)
    : impl(zc::heap<Impl>(sourceMgr, diagnosticEngine, langOpts, stringPool, bufferId)) {}

Parser::~Parser() noexcept(false) = default;

zc::Maybe<ast::Tree> Parser::parse() {
  trace::FunctionTracer functionTracer(trace::TraceCategory::kParser, __FUNCTION__);
  const size_t initialErrorCount = impl->diagnosticEngine.errorCount();
  impl->lexAll();
  impl->diagnoseTokenPatterns();
  ast::Tree tree = impl->buildTree();
  if (impl->diagnosticEngine.errorCount() != initialErrorCount) { return zc::none; }
  ZC_IF_SOME(schemaFailure, ast::verifySchemaFailure(tree)) {
    impl->diagnosticEngine.diagnose<diagnostics::DiagID::ParserInvariantViolation>(
        impl->token.getLocation(), zc::mv(schemaFailure));
  }
  if (impl->diagnosticEngine.errorCount() != initialErrorCount) { return zc::none; }
  trace::traceEvent(trace::TraceCategory::kParser, "Parse completed");
  return zc::mv(tree);
}

lexer::Token Parser::lookAhead(unsigned n) {
  lexer::LexerState state = impl->lexer.getCurrentState();
  lexer::Token saved = impl->token;
  lexer::Token result;
  for (unsigned i = 0; i < n; ++i) { impl->lexer.lex(result); }
  impl->lexer.restoreState(state);
  impl->token = zc::mv(saved);
  return result;
}

bool Parser::canLookAhead(unsigned n) { return !lookAhead(n).is(ast::SyntaxKind::EndOfFile); }

bool Parser::isLookAhead(unsigned n, ast::SyntaxKind kind) { return lookAhead(n).is(kind); }

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
