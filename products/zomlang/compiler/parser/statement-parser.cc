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

// Statement parsing implementation.

#include "zomlang/compiler/parser/parser-impl.h"

namespace zomlang {
namespace compiler {
namespace parser {

size_t Parser::Impl::findMatchingRightBrace(size_t openIndex, size_t limit) const {
  if (openIndex >= limit || kindAt(openIndex) != ast::SyntaxKind::LeftBrace) { return limit; }

  int32_t depth = 0;
  for (size_t index = openIndex; index < limit; ++index) {
    const ast::SyntaxKind kind = kindAt(index);
    if (kind == ast::SyntaxKind::EndOfFile) { return limit; }
    if (kind == ast::SyntaxKind::LeftBrace) { ++depth; }
    if (kind == ast::SyntaxKind::RightBrace) {
      --depth;
      if (depth == 0) { return index; }
    }
  }
  return limit;
}

size_t Parser::Impl::findMatchingRightBracket(size_t openIndex, size_t limit) const {
  if (openIndex >= limit || kindAt(openIndex) != ast::SyntaxKind::LeftBracket) { return limit; }

  int32_t parenDepth = 0;
  int32_t bracketDepth = 0;
  int32_t braceDepth = 0;
  for (size_t index = openIndex; index < limit; ++index) {
    const ast::SyntaxKind kind = kindAt(index);
    if (kind == ast::SyntaxKind::EndOfFile) { return limit; }
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

size_t Parser::Impl::findMatchingMacroGroup(size_t openIndex, size_t limit) const {
  if (openIndex >= limit || !isMacroGroupOpen(kindAt(openIndex))) { return limit; }

  const ast::SyntaxKind open = kindAt(openIndex);
  const ast::SyntaxKind close = macroGroupClose(open);
  int32_t depth = 0;
  for (size_t index = openIndex; index < limit; ++index) {
    const ast::SyntaxKind kind = kindAt(index);
    if (kind == ast::SyntaxKind::EndOfFile) { return limit; }
    if (kind == open) { ++depth; }
    if (kind == close) {
      --depth;
      if (depth == 0) { return index; }
    }
  }
  return limit;
}

size_t Parser::Impl::effectiveStatementStart(size_t start, size_t end) const {
  size_t head = skipOuterAttributePrefix(start, end);
  while (head < end && isDeclarationModifier(kindAt(head))) { ++head; }
  return head < end ? head : start;
}

size_t Parser::Impl::consumeSimpleStatementEnd(size_t start, size_t limit) const {
  RecoveryFrameScope recoveryFrame(*this, RecoveryContext::Statement, start);
  int32_t parenDepth = 0;
  int32_t bracketDepth = 0;
  int32_t braceDepth = 0;
  bool sawBrace = false;

  for (size_t index = start; index < limit; ++index) {
    const ast::SyntaxKind kind = kindAt(index);
    if (kind == ast::SyntaxKind::EndOfFile) {
      if (sawBrace && braceDepth > 0) {
        diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(tokenAt(index).getLocation(),
                                                                      "}"_zc);
      }
      return recoveryFrame.finish(index);
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
        } else {
          emitUnexpected(tokenAt(index));
          return recoveryFrame.finish(index + 1);
        }
        break;
      case ast::SyntaxKind::Semicolon:
        if (parenDepth == 0 && bracketDepth == 0 && braceDepth == 0) {
          return recoveryFrame.finish(index + 1);
        }
        break;
      default:
        break;
    }
  }

  if (sawBrace && braceDepth > 0) {
    diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(tokenAt(limit - 1).getLocation(),
                                                                  "}"_zc);
  }
  return recoveryFrame.finish(limit);
}

size_t Parser::Impl::consumeSpawnStatementEnd(size_t start, size_t limit) const {
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

size_t Parser::Impl::consumeStatementBodyEnd(size_t bodyStart, size_t limit) const {
  if (bodyStart >= limit) { return limit; }
  if (kindAt(bodyStart) == ast::SyntaxKind::LeftBrace) {
    return consumeBracedBodyEnd(bodyStart, limit);
  }
  TokenCursor cursor = tokenCursorAt(bodyStart);
  return consumeSourceElement(cursor, limit).end;
}

size_t Parser::Impl::consumeConditionBodyStart(size_t start, size_t limit) const {
  const size_t condStart = start + 1;
  if (condStart < limit && kindAt(condStart) == ast::SyntaxKind::LeftParen) {
    const size_t closeParen = findMatchingRightParen(condStart, limit);
    return closeParen < limit ? closeParen + 1 : limit;
  }
  TokenCursor cursor = tokenCursorAt(condStart);
  const size_t bodyOpen = consumeBalancedUntil(cursor, limit, ast::SyntaxKind::LeftBrace);
  return bodyOpen < limit ? bodyOpen : limit;
}

Parser::Impl::IfStatementParts Parser::Impl::parseIfStatementParts(size_t start,
                                                                   size_t limit) const {
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

size_t Parser::Impl::consumeIfStatementEnd(size_t start, size_t limit) const {
  return parseIfStatementParts(start, limit).end;
}

Parser::Impl::WhileStatementParts Parser::Impl::parseWhileStatementParts(size_t start,
                                                                         size_t limit) const {
  WhileStatementParts parts;
  conditionRangeAfterKeyword(start, limit, parts.condStart, parts.condEnd, parts.bodyStart);
  parts.bodyEnd = consumeStatementBodyEnd(parts.bodyStart, limit);
  parts.end = parts.bodyEnd;
  return parts;
}

size_t Parser::Impl::consumeWhileStatementEnd(size_t start, size_t limit) const {
  return parseWhileStatementParts(start, limit).end;
}

Parser::Impl::MatchStatementParts Parser::Impl::parseMatchStatementParts(size_t start,
                                                                         size_t limit) const {
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

size_t Parser::Impl::consumeMatchStatementEnd(size_t start, size_t limit) const {
  return parseMatchStatementParts(start, limit).end;
}

Parser::Impl::DoWhileStatementParts Parser::Impl::parseDoWhileStatementParts(size_t start,
                                                                             size_t limit) const {
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
  conditionRangeAfterKeyword(parts.whileIndex, limit, parts.condStart, parts.condEnd, conditionEnd);
  parts.end = conditionEnd;
  if (conditionEnd < limit && kindAt(conditionEnd) == ast::SyntaxKind::Semicolon) {
    parts.end = conditionEnd + 1;
  }
  return parts;
}

size_t Parser::Impl::consumeDoWhileStatementEnd(size_t start, size_t limit) const {
  return parseDoWhileStatementParts(start, limit).end;
}

size_t Parser::Impl::consumeLabeledStatementEnd(size_t start, size_t limit) const {
  if (start + 2 >= limit || kindAt(start + 1) != ast::SyntaxKind::Colon) {
    return consumeSimpleStatementEnd(start, limit);
  }
  return consumeStatementBodyEnd(start + 2, limit);
}

Parser::Impl::ForStatementParts Parser::Impl::parseForStatementParts(size_t head,
                                                                     size_t limit) const {
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
    TokenCursor cursor = tokenCursorAt(parts.headerStart);
    const size_t bodyOpen = consumeBalancedUntil(cursor, limit, ast::SyntaxKind::LeftBrace);
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

ast::NodeId Parser::Impl::parseBlock(AstFactory& builder, size_t openBrace, size_t limit,
                                     bool allowFinalExpression) const {
  zc::Vector<ast::NodeId> items;
  if (openBrace >= limit || kindAt(openBrace) != ast::SyntaxKind::LeftBrace) {
    return builder.makeBlockStmt(rangeFor(openBrace, openBrace), builder.makeList(items.asPtr()));
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

  return builder.makeBlockStmt(rangeFor(openBrace, bodyEnd + 1), builder.makeList(items.asPtr()));
}

ast::NodeId Parser::Impl::parseStatementBody(AstFactory& builder, size_t start, size_t end) const {
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

ast::NodeId Parser::Impl::parseLetStatement(AstFactory& builder, size_t start, size_t end) const {
  if (!requireTrailingSemicolon(start, end)) { return ast::NodeId(); }

  size_t declarationsEnd = end;
  while (start + 1 < declarationsEnd && kindAt(declarationsEnd - 1) == ast::SyntaxKind::Semicolon) {
    --declarationsEnd;
  }

  const ast::NodeId declarations = parseVariableDeclaratorList(builder, start + 1, declarationsEnd);
  if (!declarations) { return ast::NodeId(); }

  return builder.makeLetStmt(rangeFor(start, end), bindingDeclarationKindCode(kindAt(start)),
                             declarations);
}

ast::NodeId Parser::Impl::parseReturnStatement(AstFactory& builder, size_t start,
                                               size_t end) const {
  if (!requireTrailingSemicolon(start, end)) { return ast::NodeId(); }

  ast::NodeId value;
  size_t valueEnd = end;
  while (start + 1 < valueEnd && kindAt(valueEnd - 1) == ast::SyntaxKind::Semicolon) { --valueEnd; }
  if (start + 1 < valueEnd) { value = parseRequiredExpression(builder, start + 1, valueEnd); }
  return builder.makeReturnStmt(rangeFor(start, end), value);
}

ast::NodeId Parser::Impl::parseSuspendStatement(AstFactory& builder, size_t start,
                                                size_t end) const {
  if (!requireTrailingSemicolon(start, end)) { return ast::NodeId(); }

  size_t cursor = start + 1;
  size_t valueEnd = end;
  if (cursor < valueEnd && kindAt(valueEnd - 1) == ast::SyntaxKind::Semicolon) { --valueEnd; }
  if (cursor >= valueEnd) {
    return builder.makeSuspendStatement(rangeFor(start, end), ast::SuspendMode::Bare, ast::NodeId(),
                                        0);
  }

  zc::StringPtr text = tokenAt(cursor).getValue();
  if (text.size() == 0) { text = tokenLabel(tokenAt(cursor)); }
  if (kindAt(cursor) == ast::SyntaxKind::Identifier && text == "until"_zc) {
    return builder.makeSuspendStatement(rangeFor(start, end), ast::SuspendMode::Until,
                                        parseRequiredExpression(builder, cursor + 1, valueEnd), 0);
  }

  diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
      tokenAt(cursor).getLocation());
  return ast::NodeId();
}

size_t Parser::Impl::findMatchingRightParen(size_t openParen, size_t limit) const {
  if (openParen >= limit || kindAt(openParen) != ast::SyntaxKind::LeftParen) { return limit; }

  int32_t depth = 0;
  for (size_t index = openParen; index < limit; ++index) {
    const ast::SyntaxKind kind = kindAt(index);
    if (kind == ast::SyntaxKind::EndOfFile) { return limit; }
    if (kind == ast::SyntaxKind::LeftParen) { ++depth; }
    if (kind == ast::SyntaxKind::RightParen) {
      --depth;
      if (depth == 0) { return index; }
    }
  }
  return limit;
}

ast::NodeId Parser::Impl::parseIfStatement(AstFactory& builder, size_t start, size_t end) const {
  if (start + 1 >= end || kindAt(start + 1) != ast::SyntaxKind::LeftParen) {
    diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(start + 1), "("_zc);
    return ast::NodeId();
  }

  const IfStatementParts parts = parseIfStatementParts(start, end);

  ast::NodeId elseStmt;
  if (parts.elseIndex < end) { elseStmt = parseStatementBody(builder, parts.elseStart, end); }
  return builder.makeIfStmt(rangeFor(start, end),
                            parseExpressionRange(builder, parts.condStart, parts.condEnd),
                            parseStatementBody(builder, parts.thenStart, parts.thenEnd), elseStmt);
}

ast::NodeId Parser::Impl::parseWhileStatement(AstFactory& builder, size_t start, size_t end) const {
  if (start + 1 >= end || kindAt(start + 1) != ast::SyntaxKind::LeftParen) {
    diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(start + 1), "("_zc);
    return ast::NodeId();
  }

  const WhileStatementParts parts = parseWhileStatementParts(start, end);

  return builder.makeWhileStmt(rangeFor(start, end),
                               parseExpressionRange(builder, parts.condStart, parts.condEnd),
                               parseStatementBody(builder, parts.bodyStart, parts.bodyEnd));
}

ast::NodeId Parser::Impl::parseDoWhileStatement(AstFactory& builder, size_t start,
                                                size_t end) const {
  const DoWhileStatementParts parts = parseDoWhileStatementParts(start, end);
  ast::NodeId cond;
  if (parts.whileIndex < end) {
    cond = parseExpressionRange(builder, parts.condStart, parts.condEnd);
  }
  return builder.makeDoWhileStatement(
      rangeFor(start, end), parseStatementBody(builder, parts.bodyStart, parts.bodyEnd), cond);
}

ast::NodeId Parser::Impl::parseBreakStatement(AstFactory& builder, size_t start, size_t end) const {
  if (!requireTrailingSemicolon(start, end)) { return ast::NodeId(); }

  ast::IdentId label;
  if (start + 1 < end && kindAt(start + 1) == ast::SyntaxKind::Identifier) {
    label = internIdent(builder, start + 1);
  }
  return builder.makeBreakStmt(rangeFor(start, end), label);
}

ast::NodeId Parser::Impl::parseContinueStatement(AstFactory& builder, size_t start,
                                                 size_t end) const {
  if (!requireTrailingSemicolon(start, end)) { return ast::NodeId(); }

  ast::IdentId label;
  if (start + 1 < end && kindAt(start + 1) == ast::SyntaxKind::Identifier) {
    label = internIdent(builder, start + 1);
  }
  return builder.makeContinueStatement(rangeFor(start, end), label);
}

ast::NodeId Parser::Impl::parseLabeledStatement(AstFactory& builder, size_t start,
                                                size_t end) const {
  if (isOuterAttributeStart(start + 2, end)) {
    diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
        tokenAt(start + 2).getLocation());
    return ast::NodeId();
  }

  ast::NodeId statement;
  if (start + 2 < end) { statement = parseStatementBody(builder, start + 2, end); }
  return builder.makeLabeledStatement(rangeFor(start, end), internIdent(builder, start), statement);
}

ast::NodeId Parser::Impl::parseForStatement(AstFactory& builder, size_t start, size_t end) const {
  if (start + 1 >= end || kindAt(start + 1) != ast::SyntaxKind::LeftParen) {
    diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(start + 1), "("_zc);
    return ast::NodeId();
  }

  const ForStatementParts parts = parseForStatementParts(start, end);
  if (parts.firstSemi >= parts.headerEnd || parts.secondSemi >= parts.headerEnd) {
    diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(parts.headerEnd),
                                                                  ";"_zc);
    return ast::NodeId();
  }

  ast::NodeId initNode;
  if (parts.headerStart < parts.firstSemi) {
    TokenCursor cursor = tokenCursorAt(parts.headerStart);
    const size_t initEnd =
        parts.firstSemi < parts.headerEnd ? parts.firstSemi + 1 : parts.firstSemi;
    const SourceElementParseResult init = parseSourceElement(builder, cursor, initEnd);
    initNode = init.node;
  }
  ast::NodeId cond;
  if (parts.firstSemi < parts.headerEnd && parts.firstSemi + 1 < parts.secondSemi) {
    cond = parseExpressionRange(builder, parts.firstSemi + 1, parts.secondSemi);
  }
  ast::NodeId update;
  if (parts.secondSemi < parts.headerEnd && parts.secondSemi + 1 < parts.headerEnd) {
    update = parseExpressionRange(builder, parts.secondSemi + 1, parts.headerEnd);
  }
  return builder.makeForStmt(rangeFor(start, end), initNode, cond, update,
                             parseStatementBody(builder, parts.bodyStart, parts.bodyEnd));
}

ast::NodeId Parser::Impl::parseForInStatement(AstFactory& builder, size_t start, size_t end) const {
  const ForStatementParts parts = parseForStatementParts(start, end);

  ast::NodeId binding;
  if (parts.headerStart < parts.inIndex) {
    size_t bindingStart = parts.headerStart;
    if (kindAt(bindingStart) == ast::SyntaxKind::LetKeyword ||
        kindAt(bindingStart) == ast::SyntaxKind::MutKeyword) {
      ++bindingStart;
    }
    binding = parsePatternRange(builder, bindingStart, parts.inIndex);
  }
  ast::NodeId expression;
  if (parts.inIndex < parts.headerEnd && parts.inIndex + 1 < parts.headerEnd) {
    expression = parseExpressionRange(builder, parts.inIndex + 1, parts.headerEnd);
  }
  return builder.makeForInStatement(rangeFor(start, end), binding, expression,
                                    parseStatementBody(builder, parts.bodyStart, parts.bodyEnd));
}

ast::NodeId Parser::Impl::parseMatchStatement(AstFactory& builder, size_t start, size_t end) const {
  const MatchStatementParts parts = parseMatchStatementParts(start, end);

  zc::Vector<ast::NodeId> arms;
  if (parts.bodyOpen < end && kindAt(parts.bodyOpen) == ast::SyntaxKind::LeftBrace) {
    const size_t bodyEnd = parts.bodyClose;
    size_t cursor = parts.bodyOpen + 1;
    while (cursor < bodyEnd) {
      if (kindAt(cursor) == ast::SyntaxKind::WhenKeyword) {
        TokenCursor armCursor = tokenCursorAt(cursor + 1);
        const size_t arrow =
            consumeBalancedUntil(armCursor, bodyEnd, ast::SyntaxKind::EqualsGreaterThan);
        if (arrow >= bodyEnd) {
          diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(cursor + 1),
                                                                        "=>"_zc);
          return ast::NodeId();
        }

        TokenCursor guardCursor = tokenCursorAt(cursor + 1);
        const size_t guard = consumeBalancedUntil(guardCursor, arrow, ast::SyntaxKind::IfKeyword);
        const size_t patternEnd = guard < arrow ? guard : arrow;
        const size_t statementStart = arrow + 1;
        const size_t statementEnd = consumeStatementBodyEnd(statementStart, bodyEnd);

        ast::NodeId guardExpr;
        if (guard < arrow) { guardExpr = parseRequiredExpression(builder, guard + 1, arrow); }
        arms.add(builder.makeMatchArmStmt(
            rangeFor(cursor, statementEnd), parsePatternRange(builder, cursor + 1, patternEnd),
            guardExpr, parseStatementBody(builder, statementStart, statementEnd)));
        cursor = statementEnd > cursor ? statementEnd : cursor + 1;
        continue;
      }

      if (kindAt(cursor) == ast::SyntaxKind::DefaultKeyword) {
        TokenCursor armCursor = tokenCursorAt(cursor + 1);
        const size_t arrow =
            consumeBalancedUntil(armCursor, bodyEnd, ast::SyntaxKind::EqualsGreaterThan);
        if (arrow >= bodyEnd) {
          diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(cursor + 1),
                                                                        "=>"_zc);
          return ast::NodeId();
        }

        const size_t statementStart = arrow + 1;
        const size_t statementEnd = consumeStatementBodyEnd(statementStart, bodyEnd);

        arms.add(builder.makeMatchArmStmt(
            rangeFor(cursor, statementEnd),
            builder.makeWildcardPattern(rangeFor(cursor, cursor + 1), ast::NodeId()), ast::NodeId(),
            parseStatementBody(builder, statementStart, statementEnd)));
        cursor = statementEnd > cursor ? statementEnd : cursor + 1;
        continue;
      }

      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(tokenAt(cursor).getLocation(),
                                                                    "when"_zc);
      return ast::NodeId();
    }
  }

  return builder.makeMatchStmt(
      rangeFor(start, end), parseExpressionRange(builder, parts.scrutineeStart, parts.scrutineeEnd),
      builder.makeList(arms.asPtr()));
}

ast::NodeId Parser::Impl::parseExternBlockDeclaration(AstFactory& builder, size_t start,
                                                      size_t end) const {
  size_t cursor = start;
  if (isSoftKeyword(cursor, "unsafe"_zc)) { ++cursor; }
  if (cursor >= end || !isSoftKeyword(cursor, "extern"_zc)) {
    diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(diagnosticLoc(cursor));
    return ast::NodeId();
  }
  ++cursor;

  ast::Abi abi = ast::Abi::Cdecl;
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

  return builder.makeExternBlock(rangeFor(start, end), abi, builder.makeList(items.asPtr()));
}

ast::NodeId Parser::Impl::makeImplIfaceList(AstFactory& builder, size_t start, size_t end) const {
  zc::Vector<ast::NodeId> ifaces;
  size_t cursor = start;
  while (cursor < end) {
    TokenCursor ifaceCursor = tokenCursorAt(cursor);
    const size_t plus = consumeBalancedTypeUntil(ifaceCursor, end, ast::SyntaxKind::Plus);
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

  return builder.makeImplIfaceList(rangeFor(start, end), static_cast<uint8_t>(ifaces.size()),
                                   builder.makeList(ifaces.asPtr()));
}

ast::NodeId Parser::Impl::parseSpawnStatement(AstFactory& builder, size_t start, size_t end) const {
  size_t exprEnd = end;
  if (start < exprEnd && kindAt(exprEnd - 1) == ast::SyntaxKind::Semicolon) { --exprEnd; }

  const ast::NodeId expr = parseRequiredExpression(builder, start, exprEnd);
  if (!expr) { return ast::NodeId(); }

  return builder.makeExpressionStatement(rangeFor(start, end), expr);
}

bool Parser::Impl::canContinueLetInitializerBefore(size_t index) const {
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

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
