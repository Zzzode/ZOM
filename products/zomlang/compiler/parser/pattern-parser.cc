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

// Pattern parsing implementation.

#include "zomlang/compiler/parser/parser-impl.h"

namespace zomlang {
namespace compiler {
namespace parser {

void Parser::Impl::diagnoseTokenPatterns() {
  int32_t braceDepth = 0;
  bool sawTopLevelBlock = false;
  bool waitingForInterfaceBody = false;
  int32_t interfaceBodyDepth = -1;
  int32_t typeLiteralBraceDepth = -1;
  int32_t bindingPatternBraceDepth = -1;
  int32_t matchArmPatternBraceDepth = -1;
  size_t macroTokenTreeEnd = 0;

  const size_t count = context.bufferedTokenLimit();
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

    if (kind == ast::SyntaxKind::WhenKeyword && matchArmPatternBraceDepth < 0) {
      TokenCursor matchArmCursor = tokenCursorAt(i + 1);
      if (consumeBalancedUntil(matchArmCursor, count, ast::SyntaxKind::EqualsGreaterThan) < count) {
        matchArmPatternBraceDepth = braceDepth;
      }
    }
    bool insideMatchArmPattern = matchArmPatternBraceDepth >= 0;
    if (!insideMatchArmPattern && braceDepth > 0) {
      for (size_t j = i; j > 0;) {
        --j;
        if (kindAt(j) == ast::SyntaxKind::EqualsGreaterThan ||
            kindAt(j) == ast::SyntaxKind::Semicolon) {
          break;
        }
        if (kindAt(j) == ast::SyntaxKind::WhenKeyword) {
          TokenCursor matchArmCursor = tokenCursorAt(j + 1);
          insideMatchArmPattern = consumeBalancedUntil(matchArmCursor, count,
                                                       ast::SyntaxKind::EqualsGreaterThan) < count;
          break;
        }
      }
    }
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
          diagnosticEngine.diagnose<diagnostics::DiagID::IdentifierExpected>(current.getLocation());
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
            diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(tokenAt(j).getLocation(),
                                                                          "("_zc);
            break;
          }
        }
      }

      if (insideInterfaceTopLevel && kind == ast::SyntaxKind::LetKeyword && i + 2 < count &&
          next == ast::SyntaxKind::Identifier && kindAt(i + 2) == ast::SyntaxKind::Arrow) {
        diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(tokenAt(i + 2).getLocation(),
                                                                      ":"_zc);
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
          diagnosticEngine.diagnose<diagnostics::DiagID::DeclarationOrStatementExpectedAfterBlock>(
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
      diagnosticEngine.diagnose<diagnostics::DiagID::MissingSemicolon>(tokenAt(i + 3).getLocation(),
                                                                       tokenLabel(tokenAt(i + 3)));
    }
  }

  if (braceDepth > 0 && count > 0) {
    diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(tokenAt(count - 1).getLocation(),
                                                                  "}"_zc);
  }
  if (waitingForInterfaceBody && count > 0) {
    diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(tokenAt(count - 1).getLocation(),
                                                                  "{"_zc);
  }
}

ast::NodeId Parser::Impl::makeEmptyMacroPattern(AstFactory& builder, size_t start,
                                                size_t end) const {
  zc::Vector<ast::NodeId> frags;
  return builder.makeMacroPattern(rangeFor(start, end), builder.makeList(frags.asPtr()));
}

ast::NodeId Parser::Impl::parsePatternRange(AstFactory& builder, size_t start, size_t end) const {
  RecoveryFrameScope recoveryFrame(*this, RecoveryContext::Pattern, start);
  if (start >= end) { return ast::NodeId(); }

  TokenCursor bindingCursor = tokenCursorAt(start);
  const size_t at = consumeBalancedUntil(bindingCursor, end, ast::SyntaxKind::At);
  if (at < end) {
    TokenCursor duplicateBindingCursor = tokenCursorAt(at + 1);
    if (at == start || at + 1 >= end ||
        consumeBalancedUntil(duplicateBindingCursor, end, ast::SyntaxKind::At) < end) {
      if (!shouldSuppressDiagnostic(at)) {
        diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
            tokenAt(at).getLocation());
      }
      return ast::NodeId();
    }
    if (kindAt(start) != ast::SyntaxKind::Identifier || tokenAt(start).getValue() == "_"_zc) {
      if (!shouldSuppressDiagnostic(start)) {
        diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
            tokenAt(start).getLocation());
      }
      return ast::NodeId();
    }
    return builder.makeBindingPattern(rangeFor(start, end), internIdent(builder, start), false,
                                      false, parsePatternRange(builder, at + 1, end));
  }

  if (kindAt(start) == ast::SyntaxKind::IsKeyword) {
    if (start + 1 >= end) {
      if (!shouldSuppressDiagnostic(end)) {
        diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(end));
      }
      return ast::NodeId();
    }
    const ast::NodeId ty = parseTypeRange(builder, start + 1, end);
    if (!ty) {
      if (!shouldSuppressDiagnostic(start + 1)) {
        diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(start + 1));
      }
      return ast::NodeId();
    }
    return builder.makeIsPattern(rangeFor(start, end), ty);
  }

  if (rangeIsWrapped(start, end, ast::SyntaxKind::LeftParen, ast::SyntaxKind::RightParen)) {
    TokenCursor commaCursor = tokenCursorAt(start + 1);
    const bool hasComma =
        consumeBalancedUntil(commaCursor, end - 1, ast::SyntaxKind::Comma) < end - 1;
    if (start + 1 < end - 1 && !hasComma && kindAt(start + 1) != ast::SyntaxKind::DotDotDot) {
      const ast::NodeId expr = parseExpressionRange(builder, start + 1, end - 1);
      if (expr) { return builder.makeExpressionPattern(rangeFor(start, end), expr); }
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
            if (!shouldSuppressDiagnostic(itemStart)) {
              diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
                  tokenAt(itemStart).getLocation());
            }
            return ast::NodeId();
          }
          ast::IdentId binding;
          if (itemStart + 1 < itemEnd && kindAt(itemStart + 1) == ast::SyntaxKind::Identifier) {
            binding = internIdent(builder, itemStart + 1);
          }
          pats.add(builder.makeRestPattern(rangeFor(itemStart, itemEnd), binding));
        } else {
          addNodeIfPresent(pats, parsePatternRange(builder, itemStart, itemEnd));
        }
      }
      if (cursor.position() < listEnd && cursor.peek() == ast::SyntaxKind::Comma) {
        cursor.advance();
      }
    }
    return builder.makeTuplePattern(rangeFor(start, end), builder.makeList(pats.asPtr()));
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
            if (!shouldSuppressDiagnostic(itemStart)) {
              diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
                  tokenAt(itemStart).getLocation());
            }
            return ast::NodeId();
          }
          ast::IdentId binding;
          if (itemStart + 1 < itemEnd && kindAt(itemStart + 1) == ast::SyntaxKind::Identifier) {
            binding = internIdent(builder, itemStart + 1);
          }
          rest = builder.makeRestPattern(rangeFor(itemStart, itemEnd), binding);
        } else {
          addNodeIfPresent(pats, parsePatternRange(builder, itemStart, itemEnd));
        }
      }
      if (cursor.position() < listEnd && cursor.peek() == ast::SyntaxKind::Comma) {
        cursor.advance();
      }
    }
    return builder.makeArrayPattern(rangeFor(start, end), builder.makeList(pats.asPtr()), rest);
  }

  // Detect enum patterns (e.g., `Some(x)`, `Module.Ok(val)`) by forward-scanning
  // to find the identifier path end, then checking for a trailing `(...)`.
  if (kindAt(start) == ast::SyntaxKind::Identifier) {
    const size_t pathEnd = findTypePathEnd(start, end);
    if (pathEnd > start && pathEnd < end && kindAt(pathEnd) == ast::SyntaxKind::LeftParen &&
        rangeIsWrapped(pathEnd, end, ast::SyntaxKind::LeftParen, ast::SyntaxKind::RightParen)) {
      zc::Vector<ast::NodeId> args;
      const size_t listEnd = end - 1;
      TokenCursor cursor = tokenCursorAt(pathEnd + 1);
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
      return builder.makeEnumPattern(rangeFor(start, end), makeModulePath(builder, start, pathEnd),
                                     builder.makeList(args.asPtr()));
    }
  }

  size_t structStart = start;
  ast::NodeId structTyPath;
  if (kindAt(start) == ast::SyntaxKind::Identifier) {
    TokenCursor braceCursor = tokenCursorAt(start + 1);
    const size_t brace = consumeBalancedUntil(braceCursor, end, ast::SyntaxKind::LeftBrace);
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
            if (!shouldSuppressDiagnostic(itemStart)) {
              diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
                  tokenAt(itemStart).getLocation());
            }
            return ast::NodeId();
          }
          ast::IdentId binding;
          if (itemStart + 1 < itemEnd && kindAt(itemStart + 1) == ast::SyntaxKind::Identifier) {
            binding = internIdent(builder, itemStart + 1);
          }
          rest = builder.makeRestPattern(rangeFor(itemStart, itemEnd), binding);
        } else {
          TokenCursor colonCursor = tokenCursorAt(itemStart);
          const size_t colon = consumeBalancedUntil(colonCursor, itemEnd, ast::SyntaxKind::Colon);
          if (kindAt(itemStart) != ast::SyntaxKind::Identifier) {
            if (!shouldSuppressDiagnostic(itemStart)) {
              diagnosticEngine.diagnose<diagnostics::DiagID::IdentifierExpected>(
                  diagnosticLoc(itemStart));
            }
            return ast::NodeId();
          }
          ast::NodeId pat;
          bool shortForm = false;
          if (colon < itemEnd) {
            pat = parsePatternRange(builder, colon + 1, itemEnd);
          } else {
            shortForm = true;
          }
          properties.add(builder.makePatternProperty(
              rangeFor(itemStart, itemEnd), internIdent(builder, itemStart), shortForm, pat));
        }
      }
      if (cursor.position() < listEnd && cursor.peek() == ast::SyntaxKind::Comma) {
        cursor.advance();
      }
    }

    return builder.makeStructPattern(rangeFor(start, end), structTyPath,
                                     builder.makeList(properties.asPtr()), rest);
  }

  if (end == start + 1 && kindAt(start) == ast::SyntaxKind::Identifier) {
    return builder.makeIdentifierPattern(rangeFor(start, end), internIdent(builder, start),
                                         ast::NodeId());
  }

  if (end == start + 1 && kindAt(start) == ast::SyntaxKind::Underscore) {
    return builder.makeWildcardPattern(rangeFor(start, end), ast::NodeId());
  }

  if (end == start + 1 && isLiteralPatternToken(kindAt(start))) {
    const ast::NodeId literal = parseExpressionRange(builder, start, end);
    if (!literal) { return ast::NodeId(); }
    return builder.makeLiteralPattern(rangeFor(start, end), literal);
  }

  if (rangeIsWrapped(start, end, ast::SyntaxKind::LeftParen, ast::SyntaxKind::RightParen)) {
    const ast::NodeId expr = parseExpressionRange(builder, start + 1, end - 1);
    if (expr) { return builder.makeExpressionPattern(rangeFor(start, end), expr); }
  }

  return ast::NodeId();
}

size_t Parser::Impl::consumeVariableDeclaratorPattern(TokenCursor& cursor, size_t limit) const {
  int32_t parenDepth = 0;
  int32_t bracketDepth = 0;
  int32_t braceDepth = 0;

  while (cursor.position() < limit) {
    const ast::SyntaxKind kind = cursor.peek();
    if (kind == ast::SyntaxKind::EndOfFile) { return cursor.position(); }
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

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
