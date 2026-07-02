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

// Expression parsing implementation.

#include "zomlang/compiler/parser/parser-impl.h"

namespace zomlang {
namespace compiler {
namespace parser {

bool Parser::Impl::containsUnmodeledRangeOperator(size_t start, size_t end) const {
  for (size_t index = start; index < end; ++index) {
    if (kindAt(index) == ast::SyntaxKind::DotDotDot) { return true; }
    if (index + 1 < end && kindAt(index) == ast::SyntaxKind::Period &&
        kindAt(index + 1) == ast::SyntaxKind::Period) {
      return true;
    }
  }
  return false;
}

void Parser::Impl::diagnoseExpressionExpected(size_t index) const {
  diagnosticEngine.diagnose<diagnostics::DiagID::ExpressionExpected>(diagnosticLoc(index));
}

ast::NodeId Parser::Impl::parseRequiredExpression(AstFactory& builder, size_t start,
                                                  size_t end) const {
  const ast::NodeId expr = parseExpressionRange(builder, start, end);
  if (!expr) { diagnoseExpressionExpected(start); }
  return expr;
}

bool Parser::Impl::requireTrailingSemicolon(size_t start, size_t end) const {
  if (start < end && kindAt(end - 1) == ast::SyntaxKind::Semicolon) { return true; }
  diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(end), ";"_zc);
  return false;
}

bool Parser::Impl::looksLikeObjectLiteralExpression(size_t start, size_t end) const {
  if (!rangeIsWrapped(start, end, ast::SyntaxKind::LeftBrace, ast::SyntaxKind::RightBrace)) {
    return false;
  }
  TokenCursor colonCursor = tokenCursorAt(start + 1);
  TokenCursor spreadCursor = tokenCursorAt(start + 1);
  return consumeBalancedUntil(colonCursor, end - 1, ast::SyntaxKind::Colon) < end - 1 ||
         consumeBalancedUntil(spreadCursor, end - 1, ast::SyntaxKind::DotDotDot) < end - 1;
}

size_t Parser::Impl::findStructLiteralBrace(size_t start, size_t end) const {
  TokenCursor braceCursor = tokenCursorAt(start);
  const size_t brace = consumeBalancedUntil(braceCursor, end, ast::SyntaxKind::LeftBrace);
  if (brace <= start || brace >= end) { return end; }
  if (!rangeIsWrapped(brace, end, ast::SyntaxKind::LeftBrace, ast::SyntaxKind::RightBrace)) {
    return end;
  }
  return isStructLiteralTypeReference(start, brace) ? brace : end;
}

bool Parser::Impl::isDefinitelyNonLValueOperand(size_t start, size_t end) const {
  while (rangeIsWrapped(start, end, ast::SyntaxKind::LeftParen, ast::SyntaxKind::RightParen)) {
    ++start;
    --end;
  }
  return start < end && isLiteralExpressionToken(kindAt(start));
}

size_t Parser::Impl::findExpressionBinaryOperator(size_t start, size_t end) const {
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
      if (precedence > 0 && (index == start || !canEndExpressionBeforeBinary(kindAt(index - 1)))) {
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

size_t Parser::Impl::findExpressionAssignmentOperator(size_t start, size_t end) const {
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

size_t Parser::Impl::findExpressionConditionalColon(size_t question, size_t end) const {
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

size_t Parser::Impl::consumeCommaDelimitedItem(TokenCursor& cursor, size_t end) const {
  int32_t parenDepth = 0;
  int32_t bracketDepth = 0;
  int32_t braceDepth = 0;

  while (cursor.position() < end) {
    const ast::SyntaxKind kind = cursor.peek();
    if (parenDepth == 0 && bracketDepth == 0 && braceDepth == 0 && kind == ast::SyntaxKind::Comma) {
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

ast::NodeId Parser::Impl::parseExpressionList(AstFactory& builder, size_t start, size_t end,
                                              ast::SyntaxKind containerKind) const {
  zc::Vector<ast::NodeId> expressions;
  TokenCursor cursor = tokenCursorAt(start);
  while (cursor.position() < end) {
    const size_t itemStart = cursor.position();
    const size_t itemEnd = consumeCommaDelimitedItem(cursor, end);
    if (itemStart < itemEnd) {
      TokenCursor restCursor = tokenCursorAt(itemStart);
      const size_t rest = consumeBalancedUntil(restCursor, itemEnd, ast::SyntaxKind::DotDotDot);
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
  if (containerKind == ast::SyntaxKind::ArrayLiteral) {
    return builder.makeArrayLiteral(rangeFor(start, end), list);
  }
  return builder.makeTupleLiteral(rangeFor(start, end), list);
}

ast::NodeId Parser::Impl::parseCommaExpression(AstFactory& builder, size_t start,
                                               size_t end) const {
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
  return builder.makeCommaExpr(rangeFor(start, end), builder.makeList(expressions.asPtr()));
}

ast::NodeList Parser::Impl::parseExpressionArguments(AstFactory& builder, size_t start,
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

size_t Parser::Impl::findTrailingCallOpen(size_t start, size_t end) const {
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

size_t Parser::Impl::findTrailingIndexOpen(size_t start, size_t end) const {
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

size_t Parser::Impl::findTrailingMemberOperator(size_t start, size_t end) const {
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

bool Parser::Impl::canUseRangeAsCallCallee(size_t start, size_t end) const {
  if (start >= end) { return false; }
  TokenCursor commaCursor = tokenCursorAt(start);
  if (consumeBalancedUntil(commaCursor, end, ast::SyntaxKind::Comma) < end) { return false; }
  if (findExpressionAssignmentOperator(start, end) < end) { return false; }
  TokenCursor questionCursor = tokenCursorAt(start);
  if (consumeBalancedUntil(questionCursor, end, ast::SyntaxKind::Question) < end) { return false; }
  TokenCursor coalesceCursor = tokenCursorAt(start);
  if (consumeBalancedUntil(coalesceCursor, end, ast::SyntaxKind::QuestionQuestion) < end) {
    return false;
  }
  TokenCursor errorDefaultCursor = tokenCursorAt(start);
  if (consumeBalancedUntil(errorDefaultCursor, end, ast::SyntaxKind::ErrorDefault) < end) {
    return false;
  }
  return findExpressionBinaryOperator(start, end) == end;
}

ast::NodeId Parser::Impl::parseCallExpression(AstFactory& builder, size_t start, size_t openParen,
                                              size_t end) const {
  const size_t calleeEnd =
      openParen > start && kindAt(openParen - 1) == ast::SyntaxKind::QuestionDot ? openParen - 1
                                                                                 : openParen;
  const size_t typeArgsOpen = findTrailingTypeArgumentOpen(start, calleeEnd);
  const size_t parsedCalleeEnd = typeArgsOpen < calleeEnd ? typeArgsOpen : calleeEnd;
  const ast::NodeId callee = parseExpressionRange(builder, start, parsedCalleeEnd);
  if (!callee) { return ast::NodeId(); }

  ast::NodeList typeArgs;
  if (typeArgsOpen < calleeEnd && typeArgsOpen + 1 < calleeEnd - 1) {
    typeArgs = parseTypeArguments(builder, typeArgsOpen + 1, calleeEnd - 1);
  }
  return builder.makeCallExpression(rangeFor(start, end), callee, typeArgs,
                                    parseExpressionArguments(builder, openParen + 1, end - 1));
}

ast::NodeId Parser::Impl::parseNewExpression(AstFactory& builder, size_t start, size_t end) const {
  const size_t openParen = findTrailingCallOpen(start + 1, end);
  const size_t calleeEnd = openParen < end ? openParen : end;
  const size_t typeArgsOpen = findTrailingTypeArgumentOpen(start + 1, calleeEnd);
  const size_t parsedCalleeEnd = typeArgsOpen < calleeEnd ? typeArgsOpen : calleeEnd;
  const ast::NodeId callee = parseExpressionRange(builder, start + 1, parsedCalleeEnd);
  if (!callee) { return ast::NodeId(); }

  ast::NodeList typeArgs;
  if (typeArgsOpen < calleeEnd && typeArgsOpen + 1 < calleeEnd - 1) {
    typeArgs = parseTypeArguments(builder, typeArgsOpen + 1, calleeEnd - 1);
  }
  ast::NodeList args;
  if (openParen < end) { args = parseExpressionArguments(builder, openParen + 1, end - 1); }
  return builder.makeNewExpression(rangeFor(start, end), callee, typeArgs, args);
}

ast::NodeId Parser::Impl::parseMacroInvocationExpression(AstFactory& builder, size_t start,
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

  const ast::NodeId name =
      builder.makeIdentExpr(rangeFor(start, start + 1), internIdent(builder, start));

  zc::Vector<ast::NodeId> tt;
  return builder.makeMacroInvocationExpr(rangeFor(start, groupClose + 1), name,
                                         macroBraceCode(kindAt(groupOpen)),
                                         builder.makeList(tt.asPtr()));
}

ast::NodeId Parser::Impl::parseUnsafeBlockExpression(AstFactory& builder, size_t start,
                                                     size_t end) const {
  if (start + 1 >= end || kindAt(start + 1) != ast::SyntaxKind::LeftBrace) {
    diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(start + 1), "{"_zc);
    return ast::NodeId();
  }

  return builder.makeUnsafeBlockExpr(rangeFor(start, end),
                                     parseBlock(builder, start + 1, end, true));
}

ast::NodeId Parser::Impl::parseSpawnExpression(AstFactory& builder, size_t start,
                                               size_t end) const {
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

    body = builder.makeExpressionStatement(rangeFor(cursor, end), expr);
  }
  if (!body) { return ast::NodeId(); }

  return builder.makeSpawnExpression(rangeFor(start, end), modFlags, priority, body);
}

ast::NodeId Parser::Impl::parseCastExpression(AstFactory& builder, size_t start, size_t asIndex,
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

  return builder.makeCastExpression(rangeFor(start, end), mode, expr, ty);
}

ast::NodeId Parser::Impl::parseImportCallExpression(AstFactory& builder, size_t start,
                                                    size_t end) const {
  const size_t openParen = findTrailingCallOpen(start, end);
  ast::NodeList args;
  if (openParen < end) { args = parseExpressionArguments(builder, openParen + 1, end - 1); }
  return builder.makeImportCallExpression(rangeFor(start, end), args);
}

ast::NodeId Parser::Impl::parseCaptureItem(AstFactory& builder, size_t start, size_t end) const {
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

  return builder.makeCaptureItem(rangeFor(start, end), mode, internIdent(builder, nameIndex));
}

ast::NodeList Parser::Impl::parseCaptureList(AstFactory& builder, size_t start, size_t end) const {
  zc::Vector<ast::NodeId> captures;
  TokenCursor cursor = tokenCursorAt(start);
  while (cursor.position() < end) {
    while (cursor.position() < end && cursor.peek() == ast::SyntaxKind::Comma) { cursor.advance(); }
    if (cursor.position() >= end) { break; }

    const size_t itemStart = cursor.position();
    const size_t itemEnd = consumeCommaDelimitedItem(cursor, end);
    addNodeIfPresent(captures, parseCaptureItem(builder, itemStart, itemEnd));
    if (cursor.position() < end && cursor.peek() == ast::SyntaxKind::Comma) { cursor.advance(); }
  }
  return builder.makeList(captures.asPtr());
}

ast::NodeId Parser::Impl::parseFunctionExpression(AstFactory& builder, size_t start,
                                                  size_t end) const {
  size_t openParen = end;
  for (size_t index = start + 1; index < end; ++index) {
    if (kindAt(index) == ast::SyntaxKind::LeftParen) {
      openParen = index;
      break;
    }
  }
  const size_t closeParen = openParen < end ? findMatchingRightParen(openParen, end) : end;
  TokenCursor bodyCursor = tokenCursorAt(closeParen + 1);
  const size_t bodyOpen = consumeBalancedUntil(bodyCursor, end, ast::SyntaxKind::LeftBrace);
  if (openParen >= end || closeParen >= end || bodyOpen >= end) {
    diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(end), "{"_zc);
    return ast::NodeId();
  }

  const size_t signatureEnd = bodyOpen < end ? bodyOpen : end;
  TokenCursor arrowCursor = tokenCursorAt(closeParen + 1);
  const size_t arrow = consumeBalancedTypeUntil(arrowCursor, signatureEnd, ast::SyntaxKind::Arrow);
  size_t raises = end;
  if (arrow < signatureEnd) {
    TokenCursor raisesCursor = tokenCursorAt(arrow + 1);
    raises = consumeBalancedTypeUntil(raisesCursor, signatureEnd, ast::SyntaxKind::RaisesKeyword);
  }

  const ast::NodeId params = parseFunctionParameterList(builder, openParen, closeParen);

  ast::NodeList captures;
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
    TokenCursor captureCursor = tokenCursorAt(useIndex + 1);
    const size_t captureOpen = consumeBalancedUntil(captureCursor, bodyOpen < end ? bodyOpen : end,
                                                    ast::SyntaxKind::LeftBracket);
    if (captureOpen < end) {
      const size_t captureClose =
          findMatchingRightBracket(captureOpen, bodyOpen < end ? bodyOpen : end);
      const size_t captureEnd = captureClose < end ? captureClose : bodyOpen;
      captures = parseCaptureList(builder, captureOpen + 1, captureEnd);
    }
  }

  ast::NodeId retTy;
  if (arrow < signatureEnd) {
    const size_t retEnd = raises < signatureEnd ? raises : signatureEnd;
    retTy = parseTypeRange(builder, arrow + 1, retEnd);
    if (!retTy) {
      diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(arrow + 1));
      return ast::NodeId();
    }
  }
  ast::NodeId raisesTy;
  if (raises < signatureEnd) {
    raisesTy = parseTypeRange(builder, raises + 1, signatureEnd);
    if (!raisesTy) {
      diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(raises + 1));
      return ast::NodeId();
    }
  }

  return builder.makeFunctionExpression(rangeFor(start, end), params, captures, retTy, raisesTy,
                                        parseBlock(builder, bodyOpen, end));
}

ast::NodeId Parser::Impl::parseLambdaExpression(AstFactory& builder, size_t start,
                                                size_t end) const {
  if (kindAt(start) != ast::SyntaxKind::LeftParen) { return ast::NodeId(); }

  TokenCursor fatArrowCursor = tokenCursorAt(start);
  const size_t fatArrow =
      consumeBalancedUntil(fatArrowCursor, end, ast::SyntaxKind::EqualsGreaterThan);
  if (fatArrow >= end) { return ast::NodeId(); }

  const size_t closeParen = findMatchingRightParen(start, end);
  if (closeParen >= fatArrow) {
    diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(start), ")"_zc);
    return ast::NodeId();
  }

  const ast::NodeId params = parseFunctionParameterList(builder, start, closeParen);

  ast::NodeId retTy;
  ast::NodeId raisesTy;
  TokenCursor arrowCursor = tokenCursorAt(closeParen + 1);
  const size_t arrow = consumeBalancedTypeUntil(arrowCursor, fatArrow, ast::SyntaxKind::Arrow);
  if (arrow < fatArrow) {
    TokenCursor raisesCursor = tokenCursorAt(arrow + 1);
    const size_t raises =
        consumeBalancedTypeUntil(raisesCursor, fatArrow, ast::SyntaxKind::RaisesKeyword);
    const size_t retEnd = raises < fatArrow ? raises : fatArrow;
    retTy = parseTypeRange(builder, arrow + 1, retEnd);
    if (!retTy) {
      diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(arrow + 1));
      return ast::NodeId();
    }

    if (raises < fatArrow) {
      raisesTy = parseTypeRange(builder, raises + 1, fatArrow);
      if (!raisesTy) {
        diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(raises + 1));
        return ast::NodeId();
      }
    }
  }

  const size_t bodyStart = fatArrow + 1;
  if (bodyStart >= end) {
    diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(bodyStart),
                                                                  "lambda body"_zc);
    return ast::NodeId();
  }

  ast::NodeId body;
  ast::NodeId exprBody;
  if (rangeIsWrapped(bodyStart, end, ast::SyntaxKind::LeftBrace, ast::SyntaxKind::RightBrace)) {
    body = parseBlock(builder, bodyStart, end);
  } else {
    exprBody = parseExpressionRange(builder, bodyStart, end);
    if (!exprBody) { return ast::NodeId(); }
  }

  return builder.makeLambdaExpression(rangeFor(start, end), params, retTy, raisesTy, body,
                                      exprBody);
}

ast::NodeList Parser::Impl::parseObjectLiteralProperties(AstFactory& builder, size_t start,
                                                         size_t end) const {
  zc::Vector<ast::NodeId> properties;
  TokenCursor cursor = tokenCursorAt(start);
  while (cursor.position() < end) {
    const size_t itemStart = cursor.position();
    const size_t itemEnd = consumeCommaDelimitedItem(cursor, end);
    if (itemStart < itemEnd) {
      if (kindAt(itemStart) == ast::SyntaxKind::DotDotDot) {
        properties.add(builder.makeObjectSpread(
            rangeFor(itemStart, itemEnd), parseExpressionRange(builder, itemStart + 1, itemEnd)));
      } else {
        TokenCursor colonCursor = tokenCursorAt(itemStart);
        const size_t colon = consumeBalancedUntil(colonCursor, itemEnd, ast::SyntaxKind::Colon);
        ast::NodeId value;
        bool shortForm = false;
        if (colon < itemEnd) {
          value = parseExpressionRange(builder, colon + 1, itemEnd);
        } else {
          shortForm = true;
        }
        properties.add(builder.makeObjectProperty(
            rangeFor(itemStart, itemEnd), internIdent(builder, itemStart), value, shortForm));
      }
    }
    if (cursor.position() < end && cursor.peek() == ast::SyntaxKind::Comma) { cursor.advance(); }
  }

  return builder.makeList(properties.asPtr());
}

ast::NodeId Parser::Impl::parseObjectLiteralExpression(AstFactory& builder, size_t start,
                                                       size_t end) const {
  return builder.makeObjectLiteralExpr(rangeFor(start, end),
                                       parseObjectLiteralProperties(builder, start + 1, end - 1));
}

ast::NodeId Parser::Impl::parseStructLiteralExpression(AstFactory& builder, size_t start,
                                                       size_t brace, size_t end) const {
  const ast::NodeId ty = parseTypeRange(builder, start, brace);
  if (!ty) { return ast::NodeId(); }

  return builder.makeStructLiteralExpr(rangeFor(start, end), ty,
                                       parseObjectLiteralProperties(builder, brace + 1, end - 1));
}

ast::NodeId Parser::Impl::parseTemplateLiteralExpression(AstFactory& builder, size_t start,
                                                         size_t end) const {
  zc::Vector<ast::NodeId> exprs;
  size_t cursor = start + 1;
  while (cursor < end) {
    const ast::SyntaxKind kind = kindAt(cursor);
    if (kind == ast::SyntaxKind::TemplateMiddle || kind == ast::SyntaxKind::TemplateTail) {
      ++cursor;
      continue;
    }
    TokenCursor segmentCursor = tokenCursorAt(cursor);
    const size_t segmentEnd =
        consumeBalancedUntil(segmentCursor, end, ast::SyntaxKind::TemplateMiddle);
    TokenCursor tailCursor = tokenCursorAt(cursor);
    const size_t tail = consumeBalancedUntil(tailCursor, end, ast::SyntaxKind::TemplateTail);
    const size_t exprEnd = segmentEnd < tail ? segmentEnd : tail;
    if (cursor < exprEnd) {
      addNodeIfPresent(exprs, parseExpressionRange(builder, cursor, exprEnd));
    }
    cursor = exprEnd < end ? exprEnd + 1 : end;
  }

  return builder.makeTemplateLiteralExpr(rangeFor(start, end), builder.makeList(exprs.asPtr()));
}

Parser::Impl::ExpressionParseResult Parser::Impl::parseExpressionAt(AstFactory& builder,
                                                                    size_t start,
                                                                    size_t limit) const {
  return parseCommaExpressionAt(builder, start, limit);
}

Parser::Impl::ExpressionParseResult Parser::Impl::parseCommaExpressionAt(AstFactory& builder,
                                                                         size_t start,
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

  return {builder.makeCommaExpr(rangeFor(start, cursor), builder.makeList(expressions.asPtr())),
          cursor};
}

Parser::Impl::ExpressionParseResult Parser::Impl::parseAssignmentExpressionAt(AstFactory& builder,
                                                                              size_t start,
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

  return {builder.makeAssignmentExpr(
              rangeFor(start, rhs.next),
              static_cast<ast::AssignmentOperatorKind>(assignmentOpCode(kindAt(opIndex))), lhs.node,
              rhs.node),
          rhs.next};
}

Parser::Impl::ExpressionParseResult Parser::Impl::parseConditionalExpressionAt(AstFactory& builder,
                                                                               size_t start,
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

  return {builder.makeConditionalExpr(rangeFor(start, elseExpr.next), cond.node, thenExpr.node,
                                      elseExpr.node),
          elseExpr.next};
}

Parser::Impl::ExpressionParseResult Parser::Impl::parseErrorDefaultExpressionAt(
    AstFactory& builder, size_t start, size_t limit) const {
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

  return {builder.makeErrorDefaultExpr(rangeFor(start, fallback.next), primary.node, fallback.node),
          fallback.next};
}

Parser::Impl::ExpressionParseResult Parser::Impl::parseNullCoalesceExpressionAt(
    AstFactory& builder, size_t start, size_t limit) const {
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

  return {builder.makeNullCoalesceExpr(rangeFor(start, fallback.next), primary.node, fallback.node),
          fallback.next};
}

Parser::Impl::ExpressionParseResult Parser::Impl::parseBinaryExpressionAt(
    AstFactory& builder, size_t start, size_t limit, int32_t minPrecedence) const {
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

      lhs = {builder.makeCastExpression(rangeFor(start, ty.next), mode, lhs.node, ty.node),
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

      lhs = {builder.makeIsExpression(rangeFor(start, ty.next), lhs.node, ty.node), ty.next};
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

    lhs = {
        builder.makeBinaryExpr(rangeFor(start, rhs.next), binaryOpCode(kind), lhs.node, rhs.node),
        rhs.next};
    cursor = lhs.next;
  }

  return lhs;
}

Parser::Impl::ExpressionParseResult Parser::Impl::parseUnaryExpressionAt(AstFactory& builder,
                                                                         size_t start,
                                                                         size_t limit) const {
  if (start >= limit) { return ExpressionParseResult(); }

  if (kindAt(start) == ast::SyntaxKind::TypeOfKeyword) {
    ExpressionParseResult operand = parseUnaryExpressionAt(builder, start + 1, limit);
    if (!operand.node) {
      diagnoseExpressionExpected(start + 1);
      return ExpressionParseResult();
    }
    return {builder.makeTypeOfExpression(rangeFor(start, operand.next), operand.node),
            operand.next};
  }

  if (isPrefixUnaryOperator(kindAt(start))) {
    ExpressionParseResult operand = parseUnaryExpressionAt(builder, start + 1, limit);
    if (!operand.node) {
      diagnoseExpressionExpected(start + 1);
      return ExpressionParseResult();
    }
    return {builder.makeUnaryExpression(
                rangeFor(start, operand.next),
                static_cast<ast::UnaryOperatorKind>(unaryOpCode(kindAt(start))), operand.node),
            operand.next};
  }

  return parsePostfixExpressionAt(builder, start, limit);
}

Parser::Impl::ExpressionParseResult Parser::Impl::parsePostfixExpressionAt(AstFactory& builder,
                                                                           size_t start,
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

      current = {builder.makePostfixExpression(
                     rangeFor(start, cursor + 1),
                     static_cast<ast::PostfixOperatorKind>(postfixOpCode(kind)), current.node),
                 cursor + 1};
      cursor = current.next;
      continue;
    }

    if (kind == ast::SyntaxKind::RaisesKeyword && cursor + 1 < limit &&
        kindAt(cursor + 1) == ast::SyntaxKind::Question) {
      current = {
          builder.makePostfixExpression(rangeFor(start, cursor + 2),
                                        ast::PostfixOperatorKind::ErrorPropagate, current.node),
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

        current = {builder.makeCallExpression(
                       rangeFor(start, closeParen + 1), current.node,
                       parseTypeArguments(builder, cursor + 1, closeAngle),
                       parseExpressionArguments(builder, closeAngle + 2, closeParen)),
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

      current = {
          builder.makeCallExpression(rangeFor(start, closeParen + 1), current.node, ast::NodeList(),
                                     parseExpressionArguments(builder, callOpen + 1, closeParen)),
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

      current = {
          builder.makeIndexExpression(rangeFor(start, closeBracket + 1), current.node, index),
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

      current = {builder.makeMemberExpression(rangeFor(start, cursor + 2), current.node,
                                              internIdent(builder, cursor + 1)),
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

Parser::Impl::ExpressionParseResult Parser::Impl::parsePrimaryExpressionAt(AstFactory& builder,
                                                                           size_t start,
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
      return {builder.makeTupleLiteral(rangeFor(start, closeParen + 1),
                                       builder.makeList(elems.asPtr())),
              closeParen + 1};
    }

    TokenCursor commaCursor = tokenCursorAt(start + 1);
    if (consumeBalancedUntil(commaCursor, closeParen, ast::SyntaxKind::Comma) < closeParen) {
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
    TokenCursor tailCursor = tokenCursorAt(start + 1);
    const size_t tail = consumeBalancedUntil(tailCursor, limit, ast::SyntaxKind::TemplateTail);
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
    switch (kindAt(start)) {
      case ast::SyntaxKind::Identifier:
        if (tokenAt(start).getValue() == "_"_zc) { return ExpressionParseResult(); }
        return {builder.makeIdentExpr(rangeFor(start, start + 1), internIdent(builder, start)),
                start + 1};
      case ast::SyntaxKind::ThisKeyword:
        return {builder.makeThisExpr(rangeFor(start, start + 1)), start + 1};
      case ast::SyntaxKind::TrueKeyword:
      case ast::SyntaxKind::FalseKeyword:
        return {builder.makeBoolLiteral(rangeFor(start, start + 1),
                                        kindAt(start) == ast::SyntaxKind::TrueKeyword),
                start + 1};
      case ast::SyntaxKind::NullKeyword:
        return {builder.makeNullLiteral(rangeFor(start, start + 1)), start + 1};
      case ast::SyntaxKind::UnitKeyword:
        return {builder.makeUnitLiteral(rangeFor(start, start + 1)), start + 1};
      case ast::SyntaxKind::IntegerLiteral:
        return {builder.makeIntLiteral(rangeFor(start, start + 1),
                                       integerBase(tokenAt(start).getValue()),
                                       builder.internBigInt(tokenAt(start).getValue())),
                start + 1};
      case ast::SyntaxKind::BigIntLiteralToken:
        return {builder.makeBigIntLiteral(rangeFor(start, start + 1),
                                          builder.internBigInt(tokenAt(start).getValue())),
                start + 1};
      case ast::SyntaxKind::FloatLiteral:
        return {builder.makeFloatLiteralExpr(rangeFor(start, start + 1), 64,
                                             builder.internFloat(tokenAt(start).getValue())),
                start + 1};
      case ast::SyntaxKind::StringLiteral:
      case ast::SyntaxKind::CharacterLiteral:
      case ast::SyntaxKind::NoSubstitutionTemplateLiteral:
        return {builder.makeStrLiteral(rangeFor(start, start + 1), internString(builder, start),
                                       false, 0),
                start + 1};
      default:
        break;
    }

    if (isExpressionIdentifierLike(kindAt(start))) {
      return {builder.makeIdentExpr(rangeFor(start, start + 1), internIdent(builder, start)),
              start + 1};
    }
  }

  return ExpressionParseResult();
}

ast::NodeId Parser::Impl::parseExpressionRange(AstFactory& builder, size_t start,
                                               size_t end) const {
  RecoveryFrameScope recoveryFrame(*this, RecoveryContext::Expression, start);
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
  recoveryFrame.finish(end);
  return parsed.node;
}

ast::NodeId Parser::Impl::parseExpressionStatement(AstFactory& builder, size_t start,
                                                   size_t end) const {
  if (!requireTrailingSemicolon(start, end)) { return ast::NodeId(); }

  return builder.makeExpressionStatement(rangeFor(start, end),
                                         parseRequiredExpression(builder, start, end));
}

ast::NodeId Parser::Impl::parseExpressionStatementWithoutSemicolon(AstFactory& builder,
                                                                   size_t start, size_t end) const {
  const ast::NodeId expr = parseRequiredExpression(builder, start, end);
  if (!expr) { return ast::NodeId(); }

  return builder.makeExpressionStatement(rangeFor(start, end), expr);
}

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
