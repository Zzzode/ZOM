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
#include "zomlang/compiler/basic/string-pool.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/diagnostic-ids.h"
#include "zomlang/compiler/lexer/lexer.h"
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
      if (kindAt(index) == ast::SyntaxKind::Identifier) {
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

  ast::NodeId makeStatementListItem(ast::TreeBuilder& builder, ast::NodeId item,
                                    source::SourceRange range) const {
    ast::NodePayload payload;
    writeNode(payload, ast::kStatementListItemItemWord, item);
    return builder.makeNode(ast::SyntaxKind::StatementListItem, zc::mv(range), payload);
  }

  void emitUnexpected(const lexer::Token& where) {
    diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(where.getLocation());
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

  void diagnoseTokenPatterns() {
    int32_t braceDepth = 0;
    bool sawTopLevelBlock = false;
    bool waitingForInterfaceBody = false;
    int32_t interfaceBodyDepth = -1;

    const size_t count = tokenCountWithoutEof();
    for (size_t i = 0; i < count; ++i) {
      const lexer::Token& current = tokenAt(i);
      const ast::SyntaxKind kind = current.getKind();
      const ast::SyntaxKind next = i + 1 < count ? kindAt(i + 1) : ast::SyntaxKind::EndOfFile;
      const bool insideInterfaceBody = interfaceBodyDepth >= 0 && braceDepth >= interfaceBodyDepth;
      const bool insideInterfaceTopLevel =
          interfaceBodyDepth >= 0 && braceDepth == interfaceBodyDepth;

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

      if (kind == ast::SyntaxKind::LeftBrace && braceDepth == 0) { sawTopLevelBlock = true; }
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
          if (braceDepth == interfaceBodyDepth) { interfaceBodyDepth = -1; }
          --braceDepth;
          if (sawTopLevelBlock && braceDepth == 0 && next == ast::SyntaxKind::Equals) {
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
    const ast::SyntaxKind first = kindAt(start);
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
        for (size_t i = start + 1; i < end; ++i) {
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
        return ast::SyntaxKind::BlockStmt;
      case ast::SyntaxKind::Identifier:
        if (start + 1 < end && kindAt(start + 1) == ast::SyntaxKind::Colon) {
          return ast::SyntaxKind::LabeledStatement;
        }
        return ast::SyntaxKind::ExpressionStatement;
      default:
        return ast::SyntaxKind::ExpressionStatement;
    }
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

  ast::NodeId parseTypeRange(ast::TreeBuilder& builder, size_t start, size_t end) const {
    while (start < end && kindAt(end - 1) == ast::SyntaxKind::Semicolon) { --end; }
    if (start >= end) { return ast::NodeId(); }

    if (rangeIsWrapped(start, end, ast::SyntaxKind::LeftParen, ast::SyntaxKind::RightParen)) {
      return parseTypeRange(builder, start + 1, end - 1);
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
      ast::NodePayload payload;
      writeNode(payload, ast::kOptionalTypeExprInnerWord, parseTypeRange(builder, start, end - 1));
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
        if (precedence > 0 && precedence < bestPrecedence) {
          bestPrecedence = precedence;
          best = index;
        }
      }
    }
    return best;
  }

  ast::NodeId parseExpressionList(ast::TreeBuilder& builder, size_t start, size_t end,
                                  ast::SyntaxKind containerKind) const {
    zc::Vector<ast::NodeId> expressions;
    size_t cursor = start;
    while (cursor < end) {
      const size_t comma = findTopLevelCommaOrEnd(cursor, end);
      if (cursor < comma) {
        addNodeIfPresent(expressions, parseExpressionRange(builder, cursor, comma));
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

  ast::NodeId parseExpressionRange(ast::TreeBuilder& builder, size_t start, size_t end) const {
    while (start < end && kindAt(end - 1) == ast::SyntaxKind::Semicolon) { --end; }
    if (start >= end) { return ast::NodeId(); }

    if (rangeIsWrapped(start, end, ast::SyntaxKind::LeftParen, ast::SyntaxKind::RightParen)) {
      if (findTopLevelToken(start + 1, end - 1, ast::SyntaxKind::Comma) < end - 1) {
        return parseExpressionList(builder, start + 1, end - 1, ast::SyntaxKind::TupleLiteral);
      }
      return parseExpressionRange(builder, start + 1, end - 1);
    }

    if (rangeIsWrapped(start, end, ast::SyntaxKind::LeftBracket, ast::SyntaxKind::RightBracket)) {
      return parseExpressionList(builder, start + 1, end - 1, ast::SyntaxKind::ArrayLiteral);
    }

    if (kindAt(end - 1) == ast::SyntaxKind::RightParen) {
      for (size_t index = end - 1; index > start;) {
        --index;
        if (kindAt(index) == ast::SyntaxKind::LeftParen &&
            rangeIsWrapped(index, end, ast::SyntaxKind::LeftParen, ast::SyntaxKind::RightParen)) {
          if (index > start) {
            zc::Vector<ast::NodeId> args;
            size_t cursor = index + 1;
            while (cursor + 1 < end) {
              const size_t comma = findTopLevelCommaOrEnd(cursor, end - 1);
              if (cursor < comma) {
                addNodeIfPresent(args, parseExpressionRange(builder, cursor, comma));
              }
              cursor = comma < end - 1 ? comma + 1 : end - 1;
            }

            ast::NodePayload payload;
            writeNode(payload, ast::kCallExpressionCalleeWord,
                      parseExpressionRange(builder, start, index));
            writeNodeList(payload, ast::kCallExpressionArgsFirstWord,
                          ast::kCallExpressionArgsSizeWord, builder.makeList(args.asPtr()));
            return builder.makeNode(ast::SyntaxKind::CallExpression, rangeFor(start, end), payload);
          }
          break;
        }
      }
    }

    const size_t nullCoalesce = findTopLevelToken(start, end, ast::SyntaxKind::QuestionQuestion);
    if (nullCoalesce < end) {
      ast::NodePayload payload;
      writeNode(payload, ast::kNullCoalesceExprPrimaryWord,
                parseExpressionRange(builder, start, nullCoalesce));
      writeNode(payload, ast::kNullCoalesceExprFallbackWord,
                parseExpressionRange(builder, nullCoalesce + 1, end));
      return builder.makeNode(ast::SyntaxKind::NullCoalesceExpr, rangeFor(start, end), payload);
    }

    const size_t errorDefault = findTopLevelToken(start, end, ast::SyntaxKind::ErrorDefault);
    if (errorDefault < end) {
      ast::NodePayload payload;
      writeNode(payload, ast::kErrorDefaultExprPrimaryWord,
                parseExpressionRange(builder, start, errorDefault));
      writeNode(payload, ast::kErrorDefaultExprFallbackWord,
                parseExpressionRange(builder, errorDefault + 1, end));
      return builder.makeNode(ast::SyntaxKind::ErrorDefaultExpr, rangeFor(start, end), payload);
    }

    const size_t binaryOperator = findTopLevelBinaryOperator(start, end);
    if (binaryOperator < end && binaryOperator > start && binaryOperator + 1 < end) {
      ast::NodePayload payload;
      payload.words[ast::kBinaryExprOpWord] = binaryOpCode(kindAt(binaryOperator));
      writeNode(payload, ast::kBinaryExprLhsWord,
                parseExpressionRange(builder, start, binaryOperator));
      writeNode(payload, ast::kBinaryExprRhsWord,
                parseExpressionRange(builder, binaryOperator + 1, end));
      return builder.makeNode(ast::SyntaxKind::BinaryExpr, rangeFor(start, end), payload);
    }

    if (end == start + 1) {
      ast::NodePayload payload;
      switch (kindAt(start)) {
        case ast::SyntaxKind::Identifier:
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
        case ast::SyntaxKind::IntegerLiteral:
          payload.words[ast::kIntLiteralBaseWord] = integerBase(tokenAt(start).getValue());
          writeBigInt(payload, ast::kIntLiteralValueWord,
                      builder.internBigInt(tokenAt(start).getValue()));
          return builder.makeNode(ast::SyntaxKind::IntLiteral, rangeFor(start, end), payload);
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

    if (isIdentifierLike(kindAt(start))) {
      ast::NodePayload payload;
      writeIdent(payload, ast::kIdentExprNameWord, internIdent(builder, start));
      return builder.makeNode(ast::SyntaxKind::IdentExpr, rangeFor(start, start + 1), payload);
    }
    return ast::NodeId();
  }

  ast::NodeId parsePatternRange(ast::TreeBuilder& builder, size_t start, size_t end) const {
    if (start >= end) { return ast::NodeId(); }

    ast::NodePayload payload;
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
          const size_t colon = findTopLevelToken(cursor, comma, ast::SyntaxKind::Colon);
          const ast::IdentId name = kindAt(cursor) == ast::SyntaxKind::Identifier
                                        ? internIdent(builder, cursor)
                                        : ast::IdentId();
          ast::NodePayload parameterPayload;
          writeIdent(parameterPayload, ast::kFunctionParameterDeclNameWord, name);
          if (colon < comma) {
            writeNode(parameterPayload, ast::kFunctionParameterDeclTyWord,
                      parseTypeRange(builder, colon + 1, comma));
          }
          parameters.add(builder.makeNode(ast::SyntaxKind::FunctionParameterDecl,
                                          rangeFor(cursor, comma), parameterPayload));
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

  ast::NodeId parseBlock(ast::TreeBuilder& builder, size_t openBrace, size_t limit) {
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
      const ast::NodeId item = parseSourceElement(builder, cursor, statementEnd);
      if (item) { items.add(makeStatementListItem(builder, item, rangeFor(cursor, statementEnd))); }
      cursor = statementEnd > cursor ? statementEnd : cursor + 1;
    }

    ast::NodePayload payload;
    writeNodeList(payload, ast::kBlockStmtStmtsFirstWord, ast::kBlockStmtStmtsSizeWord,
                  builder.makeList(items.asPtr()));
    return builder.makeNode(ast::SyntaxKind::BlockStmt, rangeFor(openBrace, bodyEnd + 1), payload);
  }

  ast::NodeId parseStatementBody(ast::TreeBuilder& builder, size_t start, size_t end) {
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

  ast::NodeId parseExportDeclaration(ast::TreeBuilder& builder, size_t start, size_t end) {
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

    ast::NodePayload payload;
    writeNode(payload, ast::kLetStmtPatternWord,
              parsePatternRange(builder, patternStart, patternEnd));
    if (colon < end) {
      writeNode(payload, ast::kLetStmtTyWord,
                parseTypeRange(builder, colon + 1, equals < end ? equals : end));
    }
    if (equals < end) {
      writeNode(payload, ast::kLetStmtInitWord, parseExpressionRange(builder, equals + 1, end));
    }
    return builder.makeNode(ast::SyntaxKind::LetStmt, rangeFor(start, end), payload);
  }

  ast::NodeId parseReturnStatement(ast::TreeBuilder& builder, size_t start, size_t end) const {
    ast::NodePayload payload;
    if (start + 1 < end) {
      writeNode(payload, ast::kReturnStmtValueWord, parseExpressionRange(builder, start + 1, end));
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

  ast::NodeId parseIfStatement(ast::TreeBuilder& builder, size_t start, size_t end) {
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

  ast::NodeId parseWhileStatement(ast::TreeBuilder& builder, size_t start, size_t end) {
    size_t condStart = start + 1;
    size_t condEnd = end;
    size_t bodyStart = end;
    conditionRangeAfterKeyword(start, end, condStart, condEnd, bodyStart);

    ast::NodePayload payload;
    writeNode(payload, ast::kWhileStmtCondWord, parseExpressionRange(builder, condStart, condEnd));
    writeNode(payload, ast::kWhileStmtBodyWord, parseStatementBody(builder, bodyStart, end));
    return builder.makeNode(ast::SyntaxKind::WhileStmt, rangeFor(start, end), payload);
  }

  ast::NodeId parseDoWhileStatement(ast::TreeBuilder& builder, size_t start, size_t end) {
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

  ast::NodeId parseLabeledStatement(ast::TreeBuilder& builder, size_t start, size_t end) {
    ast::NodePayload payload;
    writeIdent(payload, ast::kLabeledStatementLabelWord, internIdent(builder, start));
    if (start + 2 < end) {
      writeNode(payload, ast::kLabeledStatementStatementWord,
                parseStatementBody(builder, start + 2, end));
    }
    return builder.makeNode(ast::SyntaxKind::LabeledStatement, rangeFor(start, end), payload);
  }

  ast::NodeId parseForStatement(ast::TreeBuilder& builder, size_t start, size_t end) {
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

  ast::NodeId parseForInStatement(ast::TreeBuilder& builder, size_t start, size_t end) {
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

  ast::NodeId parseMatchStatement(ast::TreeBuilder& builder, size_t start, size_t end) {
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

  ast::NodeId parseFunctionDeclaration(ast::TreeBuilder& builder, size_t start, size_t end) {
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
      writeNode(payload, ast::kAliasDeclTargetWord, parseTypeRange(builder, equals + 1, end));
    }
    return builder.makeNode(ast::SyntaxKind::AliasDecl, rangeFor(start, end), payload);
  }

  ast::NodeId parseExpressionStatement(ast::TreeBuilder& builder, size_t start, size_t end) const {
    ast::NodePayload payload;
    writeNode(payload, ast::kExpressionStatementExpressionWord,
              parseExpressionRange(builder, start, end));
    return builder.makeNode(ast::SyntaxKind::ExpressionStatement, rangeFor(start, end), payload);
  }

  ast::NodeId parseSourceElement(ast::TreeBuilder& builder, size_t start, size_t end) {
    if (start >= end) { return ast::NodeId(); }

    const ast::SyntaxKind kind = classifyStatement(start, end);
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
        return parseNamedTypeDeclaration(builder, start, end, classifyStatement(start, end));
      case ast::SyntaxKind::ErrorDecl:
        return parseErrorDeclaration(builder, start, end);
      case ast::SyntaxKind::AliasDecl:
        return parseAliasDeclaration(builder, start, end);
      case ast::SyntaxKind::ReturnStmt:
        return parseReturnStatement(builder, start, end);
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
        return builder.makeNode(ast::SyntaxKind::DebuggerStatement, rangeFor(start, end));
      case ast::SyntaxKind::ExpressionStatement:
        return parseExpressionStatement(builder, start, end);
      default:
        return parseExpressionStatement(builder, start, end);
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

  size_t findStatementEndBefore(size_t start, size_t count) {
    int32_t parenDepth = 0;
    int32_t bracketDepth = 0;
    int32_t braceDepth = 0;
    bool sawBrace = false;
    const ast::SyntaxKind first = kindAt(start);

    for (size_t i = start; i < count; ++i) {
      const ast::SyntaxKind kind = kindAt(i);

      if (i > start && kind != ast::SyntaxKind::Semicolon && parenDepth == 0 && bracketDepth == 0 &&
          braceDepth == 0 && isTopLevelStart(kind) && first == ast::SyntaxKind::LetKeyword &&
          !(kind == ast::SyntaxKind::FunKeyword && i > start &&
            kindAt(i - 1) == ast::SyntaxKind::Equals)) {
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
      const ast::SyntaxKind first = kindAt(index);
      const size_t end = findStatementEnd(index);
      const ast::NodeId element = parseSourceElement(builder, index, end);

      if (first == ast::SyntaxKind::ModuleKeyword && firstSourceElement && !moduleNode) {
        moduleNode = element;
      } else {
        if (first == ast::SyntaxKind::ModuleKeyword) {
          diagnosticEngine.diagnose<diagnostics::DiagID::ModuleDeclarationMustBeFirst>(
              tokenAt(index).getLocation());
        }
        if (element) {
          statements.add(makeStatementListItem(builder, element, rangeFor(index, end)));
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
  impl->lexAll();
  impl->diagnoseTokenPatterns();
  ast::Tree tree = impl->buildTree();
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
