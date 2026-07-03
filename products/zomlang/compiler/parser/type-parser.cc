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

// Type parsing implementation.

#include "zomlang/compiler/parser/parser-impl.h"

namespace zomlang {
namespace compiler {
namespace parser {

bool Parser::Impl::isStandaloneDynTypeRange(size_t start, size_t end) const {
  return start + 1 == end && isIdentifierText(start, "dyn"_zc);
}

bool Parser::Impl::diagnoseUnsupportedVarianceInTypeParameters(size_t openAngle,
                                                               size_t closeAngle) const {
  if (openAngle >= closeAngle || isAtEnd(closeAngle)) { return false; }

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

bool Parser::Impl::diagnoseTypeParameterListSyntax(size_t openAngle, size_t closeAngle) const {
  if (kindAt(openAngle) != ast::SyntaxKind::LessThan) { return false; }

  bool found = false;
  if (isAtEnd(closeAngle)) {
    diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(tokenAt(openAngle).getLocation(),
                                                                  ">"_zc);
    return true;
  }

  if (openAngle + 1 == closeAngle) {
    diagnosticEngine.diagnose<diagnostics::DiagID::TypeParameterDeclarationExpected>(
        diagnosticLoc(openAngle + 1));
    found = true;
  }

  int32_t angleDepth = 0;
  TokenCursor cursor = tokenCursorAt(openAngle + 1);
  TokenCursor::ScopedSplitMode splitMode(cursor);
  while (cursor.position() < closeAngle) {
    const ast::SyntaxKind kind = cursor.peek();
    if (kind == ast::SyntaxKind::LessThan) {
      ++angleDepth;
      cursor.advance();
      continue;
    }
    if (kind == ast::SyntaxKind::GreaterThan) {
      if (angleDepth > 0) { --angleDepth; }
      cursor.advance();
      continue;
    }
    if (angleDepth == 0 && kind == ast::SyntaxKind::ExtendsKeyword) {
      diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
          cursor.token().getLocation());
      found = true;
    }
    cursor.advance();
  }

  return found;
}

void Parser::Impl::diagnoseDeclarationTypeParameterSyntax(size_t afterName, size_t limit) const {
  if (afterName >= limit || kindAt(afterName) != ast::SyntaxKind::LessThan) { return; }
  const size_t closeAngle = findMatchingAngleClose(afterName, limit);
  if (diagnoseTypeParameterListSyntax(afterName, closeAngle)) { return; }
  diagnoseUnsupportedVarianceInTypeParameters(afterName, closeAngle);
}

bool Parser::Impl::followsFieldTypeColonWithoutSemicolon(size_t index) const {
  const ast::SyntaxKind kind = kindAt(index);
  return kind == ast::SyntaxKind::LeftParen || kind == ast::SyntaxKind::LeftBracket ||
         kind == ast::SyntaxKind::LeftBrace || isPrimitiveTypeKeyword(kind);
}

bool Parser::Impl::isStructLiteralTypeReference(size_t start, size_t end) const {
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

size_t Parser::Impl::findTypePathEnd(size_t start, size_t end) const {
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

size_t Parser::Impl::findMatchingAngleClose(size_t openIndex, size_t limit) const {
  if (openIndex >= limit || kindAt(openIndex) != ast::SyntaxKind::LessThan) { return limit; }

  int32_t depth = 0;
  TokenCursor cursor = tokenCursorAt(openIndex);
  TokenCursor::ScopedSplitMode splitMode(cursor);
  while (cursor.position() < limit) {
    const ast::SyntaxKind kind = cursor.peek();
    if (kind == ast::SyntaxKind::EndOfFile) { return limit; }
    if (kind == ast::SyntaxKind::LessThan) {
      ++depth;
      cursor.advance();
      continue;
    }
    if (kind == ast::SyntaxKind::GreaterThan) {
      const size_t closeIndex = cursor.position();
      if (depth == 0) { return limit; }
      --depth;
      cursor.advance();
      if (depth == 0) {
        if (cursor.position() == closeIndex) { return limit; }
        return closeIndex;
      }
      continue;
    }
    cursor.advance();
  }
  return limit;
}

bool Parser::Impl::consumeBalancedAngleList(TokenCursor& cursor, size_t limit) const {
  if (cursor.position() >= limit || cursor.peek() != ast::SyntaxKind::LessThan) { return false; }

  TokenCursor::ScopedSplitMode splitMode(cursor);
  int32_t depth = 0;
  while (cursor.position() < limit) {
    const ast::SyntaxKind kind = cursor.peek();
    if (kind == ast::SyntaxKind::EndOfFile) {
      cursor.moveTo(limit);
      return false;
    }
    if (kind == ast::SyntaxKind::LessThan) {
      ++depth;
      cursor.advance();
      continue;
    }
    if (kind == ast::SyntaxKind::GreaterThan) {
      const size_t closeIndex = cursor.position();
      if (depth == 0) { break; }
      --depth;
      cursor.advance();
      if (depth == 0) {
        if (cursor.position() == closeIndex) { break; }
        return true;
      }
      continue;
    }
    cursor.advance();
  }

  cursor.moveTo(limit);
  return false;
}

size_t Parser::Impl::functionTypeParameterTypeStart(TokenCursor& cursor, size_t limit) const {
  const TokenCursor::Mark mark = cursor.mark();
  TokenCursor::ScopedSplitMode splitMode(cursor);
  int32_t parenDepth = 0;
  int32_t bracketDepth = 0;
  int32_t braceDepth = 0;
  int32_t angleDepth = 0;

  while (cursor.position() < limit) {
    const ast::SyntaxKind kind = cursor.peek();
    if (kind == ast::SyntaxKind::EndOfFile) {
      cursor.rewind(mark);
      return mark.current;
    }
    if (parenDepth == 0 && bracketDepth == 0 && braceDepth == 0 && angleDepth == 0) {
      if (kind == ast::SyntaxKind::Colon) {
        cursor.advance();
        return cursor.position();
      }
      if (kind == ast::SyntaxKind::Comma) {
        cursor.rewind(mark);
        return mark.current;
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
    } else if (kind == ast::SyntaxKind::GreaterThan) {
      if (angleDepth > 0) { --angleDepth; }
    }
    cursor.advance();
  }

  cursor.rewind(mark);
  return mark.current;
}

ast::NodeList Parser::Impl::parseFunctionTypeParameters(AstFactory& builder, size_t start,
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

    diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(cursor.position()),
                                                                  ","_zc);
    break;
  }
  return builder.makeList(params.asPtr());
}

ast::NodeId Parser::Impl::parseTupleTypeRange(AstFactory& builder, size_t start, size_t end) const {
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

    diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(cursor.position()),
                                                                  ","_zc);
    return ast::NodeId();
  }

  if (hasTrailingComma && elems.size() == 1) {
    diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(tokenAt(end - 2).getLocation());
    return ast::NodeId();
  }

  return builder.makeTupleTypeExpr(rangeFor(start, end), builder.makeList(elems.asPtr()));
}

ast::NodeId Parser::Impl::parseObjectTypeRange(AstFactory& builder, size_t start,
                                               size_t end) const {
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
      diagnosticEngine.diagnose<diagnostics::DiagID::IdentifierExpected>(diagnosticLoc(nameIndex));
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

    members.add(builder.makeObjectTypeMember(rangeFor(memberStart, cursor.position()),
                                             internIdent(builder, nameIndex), ty.node, isMut,
                                             isOptional));

    if (cursor.position() >= bodyEnd) { break; }
    if (cursor.peek() == ast::SyntaxKind::Comma || cursor.peek() == ast::SyntaxKind::Semicolon) {
      cursor.advance();
      continue;
    }

    diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(cursor.position()),
                                                                  ","_zc);
    return ast::NodeId();
  }

  return builder.makeObjectTypeExpr(rangeFor(start, end), builder.makeList(members.asPtr()));
}

Parser::Impl::TypeParseResult Parser::Impl::parseTypeExpression(AstFactory& builder,
                                                                TokenCursor& cursor,
                                                                size_t limit) const {
  return parseUnionType(builder, cursor, limit);
}

Parser::Impl::TypeParseResult Parser::Impl::parseTypeExpressionAt(AstFactory& builder, size_t start,
                                                                  size_t limit) const {
  TokenCursor cursor = tokenCursorAt(start);
  TokenCursor::ScopedSplitMode splitMode(cursor);
  TypeParseResult result = parseTypeExpression(builder, cursor, limit);
  // Consume any remaining virtual ">" tokens from a split ">>" or ">>>".
  // This ensures the returned next position reflects the full consumption
  // of multi-character right-angle tokens.
  while (cursor.position() < limit && cursor.peek() == ast::SyntaxKind::GreaterThan) {
    cursor.advance();
  }
  result.next = cursor.position();
  return result;
}

Parser::Impl::TypeParseResult Parser::Impl::parseUnionType(AstFactory& builder, TokenCursor& cursor,
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

  return TypeParseResult{
      builder.makeUnionTypeExpr(rangeFor(start, cursor.position()), builder.makeList(alts.asPtr())),
      cursor.position()};
}

Parser::Impl::TypeParseResult Parser::Impl::parseIntersectionType(AstFactory& builder,
                                                                  TokenCursor& cursor,
                                                                  size_t limit) const {
  const size_t start = cursor.position();
  zc::Vector<ast::NodeId> alts;
  TypeParseResult first = parsePostfixType(builder, cursor, limit);
  if (!first.node) { return first; }
  alts.add(first.node);

  while (cursor.position() < limit &&
         (cursor.peek() == ast::SyntaxKind::Ampersand || cursor.peek() == ast::SyntaxKind::Plus)) {
    const size_t op = cursor.position();
    cursor.advance();
    TypeParseResult next = parsePostfixType(builder, cursor, limit);
    if (!next.node) {
      diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(op + 1));
      return TypeParseResult();
    }
    alts.add(next.node);
  }

  if (alts.size() == 1) { return first; }

  return TypeParseResult{builder.makeIntersectionTypeExpr(rangeFor(start, cursor.position()),
                                                          builder.makeList(alts.asPtr())),
                         cursor.position()};
}

Parser::Impl::TypeParseResult Parser::Impl::parsePostfixType(AstFactory& builder,
                                                             TokenCursor& cursor,
                                                             size_t limit) const {
  const size_t start = cursor.position();
  TypeParseResult result = parseAtomType(builder, cursor, limit);
  if (!result.node) { return result; }

  while (cursor.position() < limit) {
    if (cursor.peek() == ast::SyntaxKind::Question ||
        cursor.peek() == ast::SyntaxKind::QuestionQuestion) {
      const size_t suffix = cursor.position();
      result.node =
          builder.makeOptionalTypeExpr(rangeFor(start, suffix + 1), result.node,
                                       cursor.peek() == ast::SyntaxKind::QuestionQuestion);
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

      ast::NodeId lenExpr;
      if (openBracket + 1 < closeBracket) {
        lenExpr = parseExpressionRange(builder, openBracket + 1, closeBracket);
        if (!lenExpr) { return TypeParseResult(); }
      }
      result.node =
          builder.makeArrayTypeExpr(rangeFor(start, closeBracket + 1), result.node, lenExpr);
      cursor.moveTo(closeBracket + 1);
      result.next = cursor.position();
      continue;
    }

    break;
  }

  return result;
}

Parser::Impl::TypeParseResult Parser::Impl::parseFunctionType(AstFactory& builder,
                                                              TokenCursor& cursor,
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

  return TypeParseResult{
      builder.makeFunctionTypeExpr(rangeFor(start, cursor.position()),
                                   parseFunctionTypeParameters(builder, openParen + 1, closeParen),
                                   ret.node, raisesTy),
      cursor.position()};
}

Parser::Impl::TypeParseResult Parser::Impl::parseParenthesizedOrTupleType(AstFactory& builder,
                                                                          TokenCursor& cursor,
                                                                          size_t limit) const {
  const size_t start = cursor.position();
  const size_t closeParen = findMatchingRightParen(start, limit);
  if (closeParen >= limit) {
    diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(start), ")"_zc);
    return TypeParseResult();
  }

  TokenCursor commaCursor = tokenCursorAt(start + 1);
  if (start + 1 == closeParen ||
      consumeBalancedTypeUntil(commaCursor, closeParen, ast::SyntaxKind::Comma) < closeParen) {
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

Parser::Impl::TypeParseResult Parser::Impl::parseBracketedType(AstFactory& builder,
                                                               TokenCursor& cursor,
                                                               size_t limit) const {
  const size_t start = cursor.position();
  const size_t closeBracket = findMatchingRightBracket(start, limit);
  if (closeBracket >= limit) {
    diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(start), "]"_zc);
    return TypeParseResult();
  }

  TokenCursor semiCursor = tokenCursorAt(start + 1);
  const size_t semi =
      consumeBalancedTypeUntil(semiCursor, closeBracket, ast::SyntaxKind::Semicolon);
  if (semi < closeBracket) {
    const ast::NodeId elem = parseTypeRange(builder, start + 1, semi);
    const ast::NodeId lenExpr = parseExpressionRange(builder, semi + 1, closeBracket);
    if (!elem || !lenExpr) { return TypeParseResult(); }
    cursor.moveTo(closeBracket + 1);
    return TypeParseResult{
        builder.makeFixedArrayTypeExpr(rangeFor(start, closeBracket + 1), elem, lenExpr),
        cursor.position()};
  }

  const ast::NodeId elem = parseTypeRange(builder, start + 1, closeBracket);
  if (!elem) { return TypeParseResult(); }
  cursor.moveTo(closeBracket + 1);
  return TypeParseResult{builder.makeSliceArrayTypeExpr(rangeFor(start, closeBracket + 1), elem),
                         cursor.position()};
}

Parser::Impl::TypeParseResult Parser::Impl::parseTypeQuery(AstFactory& builder, TokenCursor& cursor,
                                                           size_t limit) const {
  const size_t start = cursor.position();
  const size_t pathStart = start + 1;
  const size_t pathEnd = findTypePathEnd(pathStart, limit);
  if (pathEnd == pathStart) {
    diagnosticEngine.diagnose<diagnostics::DiagID::IdentifierExpected>(diagnosticLoc(pathStart));
    return TypeParseResult();
  }

  cursor.moveTo(pathEnd);
  return TypeParseResult{builder.makeTypeQueryExpr(rangeFor(start, pathEnd),
                                                   makeModulePath(builder, pathStart, pathEnd)),
                         cursor.position()};
}

ast::NodeList Parser::Impl::parseTypeArgumentList(AstFactory& builder, TokenCursor& cursor,
                                                  size_t limit, size_t& physicalEnd) const {
  zc::Vector<ast::NodeId> args;
  const size_t openAngle = cursor.position();
  physicalEnd = openAngle;
  TokenCursor::ScopedSplitMode splitMode(cursor);
  cursor.advance();

  while (cursor.position() < limit) {
    if (cursor.peek() == ast::SyntaxKind::EndOfFile) { break; }
    if (cursor.peek() == ast::SyntaxKind::GreaterThan) {
      if (args.size() == 0) {
        diagnosticEngine.diagnose<diagnostics::DiagID::TypeArgumentExpected>(
            diagnosticLoc(openAngle + 1));
      }
      const size_t closeAngle = cursor.position();
      cursor.advance();
      physicalEnd = closeAngle + 1;
      return builder.makeList(args.asPtr());
    }

    if (cursor.peek() == ast::SyntaxKind::Comma) {
      diagnosticEngine.diagnose<diagnostics::DiagID::TypeArgumentExpected>(
          diagnosticLoc(cursor.position()));
      cursor.advance();
      continue;
    }

    const size_t itemStart = cursor.position();
    TypeParseResult item = parseTypeExpression(builder, cursor, limit);
    if (!item.node) {
      diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(itemStart));
      break;
    }
    args.add(item.node);

    if (cursor.position() >= limit) { break; }
    if (cursor.peek() == ast::SyntaxKind::Comma) {
      cursor.advance();
      continue;
    }
    if (cursor.peek() == ast::SyntaxKind::GreaterThan) { continue; }

    diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(cursor.position()),
                                                                  ","_zc);
    break;
  }

  diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(tokenAt(openAngle).getLocation(),
                                                                ">"_zc);
  physicalEnd = cursor.position();
  return builder.makeList(args.asPtr());
}

Parser::Impl::TypeParseResult Parser::Impl::parseNamedType(AstFactory& builder, TokenCursor& cursor,
                                                           size_t limit) const {
  const size_t start = cursor.position();
  const size_t pathEnd = findTypePathEnd(start, limit);
  if (pathEnd == start) { return TypeParseResult(); }

  cursor.moveTo(pathEnd);
  ast::NodeList args;
  size_t rangeEnd = cursor.position();
  if (cursor.position() < limit && cursor.peek() == ast::SyntaxKind::LessThan) {
    args = parseTypeArgumentList(builder, cursor, limit, rangeEnd);
  }

  return TypeParseResult{builder.makeNamedTypeExpr(rangeFor(start, rangeEnd),
                                                   makeModulePath(builder, start, pathEnd), args),
                         cursor.position()};
}

Parser::Impl::TypeParseResult Parser::Impl::parseAtomType(AstFactory& builder, TokenCursor& cursor,
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
        diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(start), "}"_zc);
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
    const uint8_t kind = predefinedTypeCode(cursor.peek());
    cursor.advance();
    return TypeParseResult{builder.makePredefinedTypeExpr(rangeFor(start, start + 1), kind),
                           cursor.position()};
  }

  return parseNamedType(builder, cursor, limit);
}

ast::NodeId Parser::Impl::parseTypeRange(AstFactory& builder, size_t start, size_t end) const {
  RecoveryFrameScope recoveryFrame(*this, RecoveryContext::Type, start);
  while (start < end && kindAt(end - 1) == ast::SyntaxKind::Semicolon) { --end; }
  if (start >= end) { return ast::NodeId(); }

  TypeParseResult parsed = parseTypeExpressionAt(builder, start, end);
  if (!parsed.node) { return ast::NodeId(); }
  if (parsed.next != end) {
    if (!shouldSuppressDiagnostic(parsed.next)) {
      diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
          diagnosticLoc(parsed.next));
    }
    return ast::NodeId();
  }
  recoveryFrame.finish(end);
  return parsed.node;
}

ast::NodeList Parser::Impl::parseTypeArguments(AstFactory& builder, size_t start,
                                               size_t end) const {
  zc::Vector<ast::NodeId> args;
  if (start == 0 || start > end || isAtEnd(end) || kindAt(start - 1) != ast::SyntaxKind::LessThan) {
    return builder.makeList(args.asPtr());
  }

  TokenCursor cursor = tokenCursorAt(start - 1);
  size_t physicalEnd = start - 1;
  return parseTypeArgumentList(builder, cursor, end + 1, physicalEnd);
}

size_t Parser::Impl::findTrailingTypeArgumentOpen(size_t start, size_t end) const {
  if (end <= start) { return end; }

  for (size_t candidate = start; candidate < end; ++candidate) {
    if (kindAt(candidate) != ast::SyntaxKind::LessThan) { continue; }

    TokenCursor cursor = tokenCursorAt(candidate);
    TokenCursor::ScopedSplitMode splitMode(cursor);
    int32_t depth = 0;
    while (cursor.position() < end) {
      const ast::SyntaxKind kind = cursor.peek();
      if (kind == ast::SyntaxKind::LessThan) {
        ++depth;
        cursor.advance();
        continue;
      }

      if (kind == ast::SyntaxKind::GreaterThan) {
        if (depth == 0) { break; }
        --depth;
        cursor.advance();
        if (depth == 0) {
          if (cursor.position() == end) { return candidate; }
          break;
        }
        continue;
      }

      cursor.advance();
    }
  }

  return end;
}

size_t Parser::Impl::consumeTypeLike(size_t start, size_t limit) const {
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

bool Parser::Impl::isInitializerGenericAngle(size_t openAngle, size_t limit) const {
  const size_t closeAngle = findMatchingAngleClose(openAngle, limit);
  if (closeAngle >= limit || closeAngle + 1 >= limit) { return false; }

  const ast::SyntaxKind next = kindAt(closeAngle + 1);
  return next == ast::SyntaxKind::LeftParen || next == ast::SyntaxKind::LeftBrace;
}

ast::NodeId Parser::Impl::parseExternTypeAliasDecl(AstFactory& builder, size_t start,
                                                   size_t end) const {
  const size_t nameIndex = start + 1;
  TokenCursor equalsCursor = tokenCursorAt(nameIndex + 1);
  const size_t equals = consumeBalancedUntil(equalsCursor, end, ast::SyntaxKind::Equals);
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

  return builder.makeAliasDecl(rangeFor(start, end), internIdent(builder, nameIndex), ast::NodeId(),
                               parseTypeRange(builder, targetStart, targetEnd));
}

ast::NodeId Parser::Impl::parseTypeParameters(AstFactory& builder, size_t start,
                                              size_t limit) const {
  if (start >= limit || kindAt(start) != ast::SyntaxKind::LessThan) { return ast::NodeId(); }

  // Run existing diagnostics on the type parameter list.
  diagnoseDeclarationTypeParameterSyntax(start, limit);

  const size_t closeAngle = findMatchingAngleClose(start, limit);
  if (closeAngle >= limit) { return ast::NodeId(); }
  if (start + 1 >= closeAngle) { return ast::NodeId(); }  // Empty <> — already diagnosed.

  zc::Vector<ast::NodeId> params;
  size_t cursor = start + 1;

  while (cursor < closeAngle) {
    // Skip leading whitespace / commas between params.
    while (cursor < closeAngle && kindAt(cursor) == ast::SyntaxKind::Comma) { ++cursor; }
    if (cursor >= closeAngle) { break; }

    // Detect variance annotation.
    uint8_t variance = 0;  // Invariant
    if (kindAt(cursor) == ast::SyntaxKind::InKeyword) {
      variance = 2;  // Contravariant
      ++cursor;
    } else if (kindAt(cursor) == ast::SyntaxKind::OutKeyword) {
      variance = 1;  // Covariant
      ++cursor;
    }

    // Skip attribute prefix.
    cursor = skipOuterAttributePrefix(cursor, closeAngle);
    if (cursor >= closeAngle) { break; }

    // Parse parameter name.
    if (kindAt(cursor) != ast::SyntaxKind::Identifier) {
      diagnosticEngine.diagnose<diagnostics::DiagID::IdentifierExpected>(diagnosticLoc(cursor));
      break;
    }
    const size_t nameIndex = cursor;
    ++cursor;

    // Find the end of this parameter (next comma at depth 0, or closeAngle).
    size_t paramEnd = closeAngle;
    int32_t parenDepth = 0;
    int32_t bracketDepth = 0;
    int32_t braceDepth = 0;
    int32_t angleDepth = 0;
    for (size_t i = cursor; i < closeAngle; ++i) {
      const ast::SyntaxKind k = kindAt(i);
      if (k == ast::SyntaxKind::LeftParen) {
        ++parenDepth;
      } else if (k == ast::SyntaxKind::RightParen) {
        if (parenDepth > 0) { --parenDepth; }
      } else if (k == ast::SyntaxKind::LeftBracket) {
        ++bracketDepth;
      } else if (k == ast::SyntaxKind::RightBracket) {
        if (bracketDepth > 0) { --bracketDepth; }
      } else if (k == ast::SyntaxKind::LeftBrace) {
        ++braceDepth;
      } else if (k == ast::SyntaxKind::RightBrace) {
        if (braceDepth > 0) { --braceDepth; }
      } else if (k == ast::SyntaxKind::LessThan) {
        ++angleDepth;
      } else if (k == ast::SyntaxKind::GreaterThan) {
        if (angleDepth > 0) { --angleDepth; }
      } else if (k == ast::SyntaxKind::GreaterThanGreaterThan) {
        if (angleDepth > 0) { --angleDepth; }
        if (angleDepth > 0) { --angleDepth; }
      } else if (k == ast::SyntaxKind::GreaterThanGreaterThanGreaterThan) {
        if (angleDepth > 0) { --angleDepth; }
        if (angleDepth > 0) { --angleDepth; }
        if (angleDepth > 0) { --angleDepth; }
      } else if (parenDepth == 0 && bracketDepth == 0 && braceDepth == 0 && angleDepth == 0 &&
                 k == ast::SyntaxKind::Comma) {
        paramEnd = i;
        break;
      }
    }

    // Parse optional bound (`: Type`) and optional default (`= Type`).
    ast::NodeId bound;
    ast::NodeId defaultTy;

    // Search for colon and equals at depth 0 within this param.
    size_t colonPos = paramEnd;
    size_t equalsPos = paramEnd;
    int32_t pDepth = 0;
    int32_t bDepth = 0;
    int32_t brDepth = 0;
    int32_t aDepth = 0;
    for (size_t i = cursor; i < paramEnd; ++i) {
      const ast::SyntaxKind k = kindAt(i);
      if (k == ast::SyntaxKind::LeftParen) {
        ++pDepth;
      } else if (k == ast::SyntaxKind::RightParen) {
        if (pDepth > 0) { --pDepth; }
      } else if (k == ast::SyntaxKind::LeftBracket) {
        ++bDepth;
      } else if (k == ast::SyntaxKind::RightBracket) {
        if (bDepth > 0) { --bDepth; }
      } else if (k == ast::SyntaxKind::LeftBrace) {
        ++brDepth;
      } else if (k == ast::SyntaxKind::RightBrace) {
        if (brDepth > 0) { --brDepth; }
      } else if (k == ast::SyntaxKind::LessThan) {
        ++aDepth;
      } else if (k == ast::SyntaxKind::GreaterThan) {
        if (aDepth > 0) { --aDepth; }
      } else if (k == ast::SyntaxKind::GreaterThanGreaterThan) {
        if (aDepth > 0) { --aDepth; }
        if (aDepth > 0) { --aDepth; }
      } else if (k == ast::SyntaxKind::GreaterThanGreaterThanGreaterThan) {
        if (aDepth > 0) { --aDepth; }
        if (aDepth > 0) { --aDepth; }
        if (aDepth > 0) { --aDepth; }
      } else if (pDepth == 0 && bDepth == 0 && brDepth == 0 && aDepth == 0 &&
                 k == ast::SyntaxKind::Colon && colonPos == paramEnd) {
        colonPos = i;
      } else if (pDepth == 0 && bDepth == 0 && brDepth == 0 && aDepth == 0 &&
                 k == ast::SyntaxKind::Equals) {
        equalsPos = i;
        break;
      }
    }

    if (colonPos < paramEnd) {
      const size_t boundEnd = equalsPos < paramEnd ? equalsPos : paramEnd;
      // Extend to include multi-char closing angle tokens (e.g. ">>") so
      // split mode can properly close nested generics like `Foo<Bar>`.
      // Single ">" is already excluded by the exclusive-end convention.
      size_t typeLimit = boundEnd;
      if (boundEnd == closeAngle && closeAngle < limit) {
        const ast::SyntaxKind closeKind = kindAt(closeAngle);
        if (closeKind == ast::SyntaxKind::GreaterThanGreaterThan ||
            closeKind == ast::SyntaxKind::GreaterThanGreaterThanGreaterThan) {
          typeLimit = closeAngle + 1;
        }
      }
      bound = parseTypeRange(builder, colonPos + 1, typeLimit);
    }
    if (equalsPos < paramEnd) {
      size_t typeLimit = paramEnd;
      if (paramEnd == closeAngle && closeAngle < limit) {
        const ast::SyntaxKind closeKind = kindAt(closeAngle);
        if (closeKind == ast::SyntaxKind::GreaterThanGreaterThan ||
            closeKind == ast::SyntaxKind::GreaterThanGreaterThanGreaterThan) {
          typeLimit = closeAngle + 1;
        }
      }
      defaultTy = parseTypeRange(builder, equalsPos + 1, typeLimit);
    }

    params.add(builder.makeGenericTypeParam(rangeFor(nameIndex, paramEnd),
                                            internIdent(builder, nameIndex), bound, defaultTy,
                                            variance));

    cursor = paramEnd;
    if (cursor < closeAngle && kindAt(cursor) == ast::SyntaxKind::Comma) { ++cursor; }
  }

  return builder.makeGenericParams(rangeFor(start, closeAngle + 1),
                                   static_cast<uint16_t>(params.size()),
                                   builder.makeList(params.asPtr()));
}

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
