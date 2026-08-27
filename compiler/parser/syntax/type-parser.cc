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

#include "compiler/parser/parser-impl.h"

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

// RFC 0002: Validation helper, NOT a production-selection range scan.  Callers in
// parsePostfixExpressionAt (expression-parser.cc) have already identified a potential struct
// literal — either by finding '<'…'>' followed by '{' (line 853), or by seeing '{' directly
// (line 944).  This function merely validates that the tokens preceding '{' form a valid
// type path (with optional generic arguments).  Forward-only scanning via consumeTypePath
// and consumeBalancedAngleList is used to confirm the type-path shape, not to choose
// between competing productions.
bool Parser::Impl::isStructLiteralTypeReference(size_t start, size_t end) const {
  if (start >= end) { return false; }

  TokenCursor cursor = tokenCursorAt(start);
  if (!consumeTypePath(cursor, end)) { return false; }
  if (cursor.position() == end) { return true; }

  // consumeBalancedAngleList validates that the <…> generic argument list is balanced;
  // this is boundary-detection within an already-identified type path, not production
  // selection.
  return cursor.peek() == ast::SyntaxKind::LessThan && consumeBalancedAngleList(cursor, end) &&
         cursor.position() == end;
}

bool Parser::Impl::consumeTypePath(TokenCursor& cursor, size_t limit) const {
  const size_t start = cursor.position();
  bool expectSegment = true;
  bool consumedSegment = false;
  if (cursor.position() < limit && cursor.peek() == ast::SyntaxKind::ColonColon) {
    cursor.advance();
  }

  while (cursor.position() < limit) {
    const ast::SyntaxKind kind = cursor.peek();
    if (expectSegment) {
      if (kind != ast::SyntaxKind::Identifier) {
        cursor.moveTo(start);
        return false;
      }
      consumedSegment = true;
      expectSegment = false;
      cursor.advance();
      continue;
    }

    if (kind == ast::SyntaxKind::Period || kind == ast::SyntaxKind::ColonColon) {
      expectSegment = true;
      cursor.advance();
      continue;
    }
    break;
  }

  if (!consumedSegment || expectSegment) {
    cursor.moveTo(start);
    return false;
  }
  return true;
}

// RFC 0002: Boundary detection only — caller has already committed to parsing a type path.
// Returns the index one past the end of the type path starting at 'start'.
size_t Parser::Impl::findTypePathEnd(size_t start, size_t end) const {
  TokenCursor cursor = tokenCursorAt(start);
  return consumeTypePath(cursor, end) ? cursor.position() : start;
}

bool Parser::Impl::isDynAssocBindingArgList(size_t openAngle, size_t closeAngle) const {
  if (openAngle + 1 >= closeAngle) { return false; }
  size_t cursor = openAngle + 1;
  while (cursor < closeAngle && kindAt(cursor) == ast::SyntaxKind::Comma) { ++cursor; }
  return cursor + 1 < closeAngle && kindAt(cursor) == ast::SyntaxKind::Identifier &&
         kindAt(cursor + 1) == ast::SyntaxKind::Equals;
}

// RFC 0002: findMatchingRight* pattern — allowed. Caller has already seen the opening '<'
// and committed to parsing angle-delimited content. Returns the matching '>' index.
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

// RFC 0002: Boundary detection / content validation for a <…> generic argument list.
// Callers (isStructLiteralTypeReference, consumeFunctionTypeHead) have already committed to
// parsing a specific production; this function only advances the cursor through the balanced
// angle brackets and reports whether the content is well-formed.  Not used for production
// selection.
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

bool Parser::Impl::consumeFunctionTypeHead(TokenCursor& cursor, size_t limit, size_t& openParen,
                                           size_t& closeParen) const {
  const TokenCursor::Mark mark = cursor.mark();
  openParen = limit;
  closeParen = limit;

  if (cursor.position() < limit && cursor.peek() == ast::SyntaxKind::FunKeyword) {
    cursor.advance();
  }

  if (cursor.position() < limit && cursor.peek() == ast::SyntaxKind::LessThan &&
      !consumeBalancedAngleList(cursor, limit)) {
    cursor.rewind(mark);
    return false;
  }

  if (cursor.position() >= limit || cursor.peek() != ast::SyntaxKind::LeftParen) {
    cursor.rewind(mark);
    return false;
  }

  openParen = cursor.position();
  closeParen = consumeBalancedGroupEnd(cursor, limit, ast::SyntaxKind::LeftParen,
                                       ast::SyntaxKind::RightParen);
  if (closeParen >= limit || cursor.position() >= limit ||
      cursor.peek() != ast::SyntaxKind::Arrow) {
    cursor.rewind(mark);
    return false;
  }

  return true;
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

    const size_t typeStart = cursor.position();
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

  while (cursor.position() < limit && cursor.peek() == ast::SyntaxKind::Ampersand) {
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

      if (openBracket + 1 < closeBracket) {
        diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
            diagnosticLoc(openBracket + 1));
        return TypeParseResult();
      }
      result.node = builder.makeArrayTypeExpr(rangeFor(start, closeBracket + 1), result.node);
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
  size_t openParen = limit;
  size_t closeParen = limit;
  if (!consumeFunctionTypeHead(cursor, limit, openParen, closeParen)) { return TypeParseResult(); }

  cursor.advance();
  const size_t retStart = cursor.position();
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

Parser::Impl::TypeParseResult Parser::Impl::parseDynType(AstFactory& builder, TokenCursor& cursor,
                                                         size_t limit) const {
  const size_t start = cursor.position();
  if (start >= limit || !isIdentifierText(start, "dyn"_zc)) { return TypeParseResult(); }
  cursor.advance();

  zc::Vector<ast::NodeId> markers;
  zc::Vector<ast::NodeId> assocBindings;

  if (cursor.position() >= limit || cursor.peek() == ast::SyntaxKind::Plus) {
    diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(cursor.position()));
    return TypeParseResult();
  }

  const size_t ifaceStart = cursor.position();
  const size_t ifaceHeadEnd = findTypePathEnd(ifaceStart, limit);
  if (ifaceHeadEnd == ifaceStart) {
    diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(ifaceStart));
    return TypeParseResult();
  }

  size_t ifaceEnd = ifaceHeadEnd;
  if (ifaceHeadEnd < limit && kindAt(ifaceHeadEnd) == ast::SyntaxKind::LessThan) {
    const size_t genericClose = findMatchingAngleClose(ifaceHeadEnd, limit);
    if (genericClose >= limit) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(ifaceHeadEnd),
                                                                    ">"_zc);
      return TypeParseResult();
    }
    if (!isDynAssocBindingArgList(ifaceHeadEnd, genericClose)) { ifaceEnd = genericClose + 1; }
  }

  TypeParseResult iface = parsePostfixType(builder, cursor, ifaceEnd);
  if (!iface.node) {
    diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(ifaceStart));
    return TypeParseResult();
  }
  size_t assocBindingsStart = cursor.position();
  if (cursor.position() < limit && cursor.peek() == ast::SyntaxKind::LessThan) {
    const size_t openAngle = cursor.position();
    const size_t closeAngle = findMatchingAngleClose(openAngle, limit);
    if (closeAngle >= limit) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(openAngle),
                                                                    ">"_zc);
      return TypeParseResult();
    }

    TokenCursor bindingCursor = tokenCursorAt(openAngle + 1);
    while (bindingCursor.position() < closeAngle) {
      if (bindingCursor.peek() == ast::SyntaxKind::Comma) {
        bindingCursor.advance();
        continue;
      }

      const size_t bindingStart = bindingCursor.position();
      if (bindingCursor.peek() != ast::SyntaxKind::Identifier) {
        diagnosticEngine.diagnose<diagnostics::DiagID::IdentifierExpected>(
            diagnosticLoc(bindingCursor.position()));
        return TypeParseResult();
      }
      const size_t nameIndex = bindingCursor.position();
      bindingCursor.advance();

      if (bindingCursor.position() >= closeAngle ||
          bindingCursor.peek() != ast::SyntaxKind::Equals) {
        diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(
            diagnosticLoc(bindingCursor.position()), "="_zc);
        return TypeParseResult();
      }
      bindingCursor.advance();

      const size_t typeStart = bindingCursor.position();
      const size_t typeEnd =
          consumeBalancedTypeUntil(bindingCursor, closeAngle, ast::SyntaxKind::Comma);
      if (typeStart >= typeEnd) {
        diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(typeStart));
        return TypeParseResult();
      }

      ast::NodeId bindingTy = parseTypeRange(builder, typeStart, typeEnd);
      if (!bindingTy) { return TypeParseResult(); }
      assocBindings.add(builder.makeDynTypeAssocBinding(
          rangeFor(bindingStart, typeEnd), internIdent(builder, nameIndex), bindingTy));

      bindingCursor.moveTo(typeEnd);
      if (bindingCursor.position() < closeAngle && bindingCursor.peek() == ast::SyntaxKind::Comma) {
        bindingCursor.advance();
      }
    }

    cursor.moveTo(closeAngle + 1);
    iface.next = cursor.position();
  }

  while (cursor.position() < limit && cursor.peek() == ast::SyntaxKind::Plus) {
    cursor.advance();

    const size_t markerStart = cursor.position();
    const size_t markerEnd = findAttributePathEnd(markerStart, limit);
    if (markerEnd <= markerStart) {
      diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(markerStart));
      return TypeParseResult();
    }

    markers.add(makeAttributePath(builder, markerStart, markerEnd));
    cursor.moveTo(markerEnd);
  }

  ast::NodeId markerList;
  if (!markers.empty()) {
    markerList = builder.makeDynTypeMarkerList(rangeFor(iface.next + 1, cursor.position()),
                                               static_cast<uint8_t>(markers.size()),
                                               builder.makeList(markers.asPtr()));
  }
  ast::NodeId assocBindingList;
  if (!assocBindings.empty()) {
    assocBindingList = builder.makeDynTypeAssocBindingList(
        rangeFor(assocBindingsStart, iface.next), static_cast<uint8_t>(assocBindings.size()),
        builder.makeList(assocBindings.asPtr()));
  }

  return TypeParseResult{builder.makeDynTypeExpr(rangeFor(start, cursor.position()), iface.node,
                                                 markerList, assocBindingList),
                         cursor.position()};
}

Parser::Impl::TypeParseResult Parser::Impl::parseAssociatedTypeProjection(AstFactory& builder,
                                                                          TokenCursor& cursor,
                                                                          size_t limit) const {
  const size_t start = cursor.position();
  if (start >= limit || cursor.peek() != ast::SyntaxKind::LessThan) { return TypeParseResult(); }

  const size_t closeAngle = findMatchingAngleClose(start, limit);
  if (closeAngle >= limit || closeAngle + 2 >= limit ||
      kindAt(closeAngle + 1) != ast::SyntaxKind::ColonColon ||
      kindAt(closeAngle + 2) != ast::SyntaxKind::Identifier) {
    return TypeParseResult();
  }

  TokenCursor asCursor = tokenCursorAt(start + 1);
  const size_t asIndex = consumeBalancedTypeUntil(asCursor, closeAngle, ast::SyntaxKind::AsKeyword);
  if (asIndex >= closeAngle || start + 1 >= asIndex || asIndex + 1 >= closeAngle) {
    return TypeParseResult();
  }

  const ast::NodeId baseTy = parseTypeRange(builder, start + 1, asIndex);
  size_t ifaceEnd = closeAngle;
  {
    TokenCursor ifaceCursor = tokenCursorAt(asIndex + 1);
    TokenCursor::ScopedSplitMode splitMode(ifaceCursor);
    int32_t angleDepth = 0;
    while (ifaceCursor.position() < closeAngle) {
      const ast::SyntaxKind kind = ifaceCursor.peek();
      if (kind == ast::SyntaxKind::LessThan) {
        ++angleDepth;
      } else if (kind == ast::SyntaxKind::GreaterThan && angleDepth > 0) {
        --angleDepth;
      }
      ifaceCursor.advance();
    }

    const ast::SyntaxKind closeKind = kindAt(closeAngle);
    if (angleDepth > 0 && (closeKind == ast::SyntaxKind::GreaterThanGreaterThan ||
                           closeKind == ast::SyntaxKind::GreaterThanGreaterThanGreaterThan)) {
      ifaceEnd = closeAngle + 1;
    }
  }
  const ast::NodeId ifaceTy = parseTypeRange(builder, asIndex + 1, ifaceEnd);
  if (!baseTy || !ifaceTy) { return TypeParseResult(); }

  cursor.moveTo(closeAngle + 3);
  return TypeParseResult{
      builder.makeAssociatedTypeProjectionExpr(rangeFor(start, cursor.position()), baseTy, ifaceTy,
                                               internIdent(builder, closeAngle + 2)),
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

  // RFC 0002: Cursor-driven disambiguation between tuple and parenthesized type.
  // Instead of pre-scanning for a comma with consumeBalancedTypeUntil, we use
  // boundary detection to find the first element's end, then inspect the following
  // token to decide the production.
  if (start + 1 == closeParen) {
    // Empty tuple: ()
    const ast::NodeId tuple = parseTupleTypeRange(builder, start, closeParen + 1);
    if (tuple) { cursor.moveTo(closeParen + 1); }
    return TypeParseResult{tuple, tuple ? cursor.position() : start};
  }

  const size_t innerStart = start + 1;

  // Find the boundary of the first type-like construct (boundary detection per RFC 0002).
  // consumeTypeLike does not create AST nodes — it only locates the boundary.
  const size_t firstElemEnd = consumeTypeLike(innerStart, closeParen);

  if (firstElemEnd > innerStart && firstElemEnd < closeParen) {
    const ast::SyntaxKind afterFirst = kindAt(firstElemEnd);
    if (afterFirst == ast::SyntaxKind::Comma || afterFirst == ast::SyntaxKind::DotDotDot) {
      // Simple first element followed by comma or '...' — tuple type.
      const ast::NodeId tuple = parseTupleTypeRange(builder, start, closeParen + 1);
      if (tuple) { cursor.moveTo(closeParen + 1); }
      return TypeParseResult{tuple, tuple ? cursor.position() : start};
    }
    if (afterFirst == ast::SyntaxKind::Bar || afterFirst == ast::SyntaxKind::Ampersand ||
        afterFirst == ast::SyntaxKind::Arrow) {
      // First element is a compound type (union, intersection, function).
      // Fall back to parseTypeExpression to find the true element boundary.
      cursor.moveTo(innerStart);
      TypeParseResult firstElem = parseTypeExpression(builder, cursor, closeParen);
      if (!firstElem.node) { return TypeParseResult(); }
      if (cursor.position() < closeParen && (cursor.peek() == ast::SyntaxKind::Comma ||
                                             cursor.peek() == ast::SyntaxKind::DotDotDot)) {
        const ast::NodeId tuple = parseTupleTypeRange(builder, start, closeParen + 1);
        if (tuple) { cursor.moveTo(closeParen + 1); }
        return TypeParseResult{tuple, tuple ? cursor.position() : start};
      }
      // Single compound element — parenthesized type.
      if (cursor.position() != closeParen) {
        diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
            diagnosticLoc(cursor.position()));
        return TypeParseResult();
      }
      cursor.moveTo(closeParen + 1);
      firstElem.next = cursor.position();
      return firstElem;
    }
  }

  // Single simple element consumed to the closing paren — parenthesized type.
  cursor.moveTo(innerStart);
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
  if (start >= limit || cursor.peek() != ast::SyntaxKind::LeftBracket) { return TypeParseResult(); }
  cursor.advance();  // consume '['

  // Parse the element type cursor-driven. The parser stops at ';' or ']'
  // whichever comes first, respecting angle/paren/bracket nesting.
  TypeParseResult elemResult = parseTypeExpression(builder, cursor, limit);
  if (!elemResult.node) { return TypeParseResult(); }

  if (cursor.position() < limit && cursor.peek() == ast::SyntaxKind::Semicolon) {
    // Fixed array type: [T; N]
    cursor.advance();  // consume ';'
    const size_t lenStart = cursor.position();
    const size_t closeBracket = findMatchingRightBracket(start, limit);
    if (closeBracket >= limit) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(start), "]"_zc);
      return TypeParseResult();
    }
    const ast::NodeId lenExpr = parseExpressionRange(builder, lenStart, closeBracket);
    if (!lenExpr) { return TypeParseResult(); }
    cursor.moveTo(closeBracket + 1);
    return TypeParseResult{builder.makeFixedArrayTypeExpr(rangeFor(start, cursor.position()),
                                                          elemResult.node, lenExpr),
                           cursor.position()};
  }

  // Slice type: [T]
  if (cursor.position() >= limit || cursor.peek() != ast::SyntaxKind::RightBracket) {
    diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(cursor.position()),
                                                                  "]"_zc);
    return TypeParseResult();
  }
  cursor.advance();  // consume ']'
  return TypeParseResult{
      builder.makeSliceArrayTypeExpr(rangeFor(start, cursor.position()), elemResult.node),
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

  const ast::SyntaxKind atomStart = cursor.peek();
  if (atomStart == ast::SyntaxKind::FunKeyword || atomStart == ast::SyntaxKind::LessThan ||
      atomStart == ast::SyntaxKind::LeftParen) {
    if (atomStart == ast::SyntaxKind::LessThan) {
      TypeParseResult projection = parseAssociatedTypeProjection(builder, cursor, limit);
      if (projection.node) { return projection; }
      cursor.moveTo(start);
    }

    TypeParseResult functionType = parseFunctionType(builder, cursor, limit);
    if (functionType.node) { return functionType; }
    cursor.moveTo(start);
  }

  if (isIdentifierText(start, "dyn"_zc)) { return parseDynType(builder, cursor, limit); }

  switch (atomStart) {
    case ast::SyntaxKind::Ampersand: {
      cursor.advance();
      bool isMut = false;
      if (cursor.position() < limit && cursor.peek() == ast::SyntaxKind::MutKeyword) {
        isMut = true;
        cursor.advance();
      }
      TypeParseResult operand = parseTypeExpression(builder, cursor, limit);
      if (!operand.node) {
        diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(start + 1));
        return TypeParseResult();
      }
      return TypeParseResult{
          builder.makeReferenceTypeExpr(rangeFor(start, operand.next), operand.node, isMut),
          operand.next};
    }
    case ast::SyntaxKind::Asterisk: {
      cursor.advance();
      bool isMut = false;
      if (cursor.position() < limit && cursor.peek() == ast::SyntaxKind::MutKeyword) {
        isMut = true;
        cursor.advance();
      } else if (cursor.position() < limit && cursor.peek() == ast::SyntaxKind::ConstKeyword) {
        cursor.advance();
      }
      TypeParseResult operand = parseTypeExpression(builder, cursor, limit);
      if (!operand.node) {
        diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(start + 1));
        return TypeParseResult();
      }
      return TypeParseResult{
          builder.makeRawPointerTypeExpr(rangeFor(start, operand.next), operand.node, isMut),
          operand.next};
    }
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

  TokenCursor cursor = tokenCursorAt(start);
  TokenCursor::ScopedSplitMode splitMode(cursor);
  TypeParseResult parsed = parseTypeExpression(builder, cursor, end);
  while (cursor.position() < end && cursor.peek() == ast::SyntaxKind::GreaterThan) {
    cursor.advance();
  }
  parsed.next = cursor.position();
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

ast::NodeList Parser::Impl::parseBoundListMembers(AstFactory& builder, size_t start,
                                                  size_t end) const {
  zc::Vector<ast::NodeId> bounds;
  size_t cursor = start;
  while (cursor < end) {
    TypeParseResult bound = parseTypeExpressionAt(builder, cursor, end);
    if (!bound.node || bound.next <= cursor) {
      diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(cursor));
      return ast::NodeList();
    }
    bounds.add(bound.node);
    cursor = bound.next;
    if (cursor == end) { break; }
    if (kindAt(cursor) != ast::SyntaxKind::Plus) {
      diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
          diagnosticLoc(cursor));
      return ast::NodeList();
    }
    ++cursor;
    if (cursor == end) {
      diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(cursor));
      return ast::NodeList();
    }
  }

  if (bounds.empty()) {
    diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(start));
    return ast::NodeList();
  }
  return builder.makeList(bounds.asPtr());
}

ast::NodeId Parser::Impl::parseTypeParameterBoundList(AstFactory& builder, size_t start,
                                                      size_t end) const {
  const ast::NodeList bounds = parseBoundListMembers(builder, start, end);
  if (bounds.empty()) { return ast::NodeId(); }
  return builder.makeTypeParameterBoundList(rangeFor(start, end), bounds);
}

ast::NodeId Parser::Impl::parseAssociatedTypeBoundList(AstFactory& builder, size_t start,
                                                       size_t end) const {
  const ast::NodeList bounds = parseBoundListMembers(builder, start, end);
  if (bounds.empty()) { return ast::NodeId(); }
  return builder.makeAssociatedTypeBoundList(rangeFor(start, end), bounds);
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

// RFC 0002: Boundary detection only — caller has already identified the start token as
// type-like (identifier, '{', '(', '[', or primitive keyword). Scans forward to find
// where the type-like construct ends, using findMatchingRight* for delimited groups.
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

// RFC 0002: Boundary detection only — uses findMatchingAngleClose to locate the closing '>'
// of a generic argument list, then inspects the following token to distinguish initializer
// syntax (Foo<T>(...) or Foo<T>{...}) from other uses. Caller has already committed to
// parsing a generic type.
bool Parser::Impl::isInitializerGenericAngle(size_t openAngle, size_t limit) const {
  const size_t closeAngle = findMatchingAngleClose(openAngle, limit);
  if (closeAngle >= limit || closeAngle + 1 >= limit) { return false; }

  const ast::SyntaxKind next = kindAt(closeAngle + 1);
  return next == ast::SyntaxKind::LeftParen || next == ast::SyntaxKind::LeftBrace;
}

ast::NodeId Parser::Impl::parseWherePredicate(AstFactory& builder, size_t start, size_t end) const {
  while (start < end && kindAt(start) == ast::SyntaxKind::Comma) { ++start; }
  while (end > start && kindAt(end - 1) == ast::SyntaxKind::Comma) { --end; }
  if (start >= end) {
    diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(start));
    return ast::NodeId();
  }

  TokenCursor colonCursor = tokenCursorAt(start);
  const size_t colon = consumeBalancedTypeUntil(colonCursor, end, ast::SyntaxKind::Colon);
  if (colon >= end) {
    diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(start), ":"_zc);
    return ast::NodeId();
  }
  if (start >= colon || colon + 1 >= end) {
    diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(start));
    return ast::NodeId();
  }

  const ast::NodeId ty = parseTypeRange(builder, start, colon);
  const ast::NodeId bound = parseTypeRange(builder, colon + 1, end);
  if (!ty || !bound) { return ast::NodeId(); }

  return builder.makeWherePred(rangeFor(start, end), ast::WhereBoundKind::Implements, ty, bound);
}

ast::NodeId Parser::Impl::parseWhereClause(AstFactory& builder, size_t start, size_t end) const {
  if (start >= end || !isIdentifierText(start, "where"_zc)) { return ast::NodeId(); }

  zc::Vector<ast::NodeId> preds;
  size_t cursor = start + 1;
  while (cursor < end) {
    while (cursor < end && kindAt(cursor) == ast::SyntaxKind::Comma) { ++cursor; }
    if (cursor >= end) { break; }

    TokenCursor commaCursor = tokenCursorAt(cursor);
    const size_t comma = consumeBalancedTypeUntil(commaCursor, end, ast::SyntaxKind::Comma);
    const size_t predEnd = comma < end ? comma : end;
    const ast::NodeId pred = parseWherePredicate(builder, cursor, predEnd);
    if (!pred) { return ast::NodeId(); }
    preds.add(pred);
    cursor = comma < end ? comma + 1 : end;
  }

  if (preds.empty()) {
    diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(start + 1));
    return ast::NodeId();
  }

  return builder.makeWhereClause(rangeFor(start, end), builder.makeList(preds.asPtr()));
}

ast::NodeId Parser::Impl::parseTypeParameters(AstFactory& builder, size_t start, size_t limit,
                                              ast::NodeId whereClause) const {
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

    // Preserve recovery after the registered unsupported-variance diagnostic,
    // but do not materialize rejected syntax in the semantic AST.
    if (kindAt(cursor) == ast::SyntaxKind::InKeyword) {
      ++cursor;
    } else if (kindAt(cursor) == ast::SyntaxKind::OutKeyword) {
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
    ast::NodeId bounds;
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
      bounds = parseTypeParameterBoundList(builder, colonPos + 1, typeLimit);
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
                                            internIdent(builder, nameIndex), bounds, defaultTy));

    cursor = paramEnd;
    if (cursor < closeAngle && kindAt(cursor) == ast::SyntaxKind::Comma) { ++cursor; }
  }

  return builder.makeGenericParams(rangeFor(start, closeAngle + 1),
                                   static_cast<uint16_t>(params.size()),
                                   builder.makeList(params.asPtr()), whereClause);
}

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
