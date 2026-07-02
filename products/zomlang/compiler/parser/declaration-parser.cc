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

// Declaration parsing implementation.

#include "zomlang/compiler/parser/parser-impl.h"

namespace zomlang {
namespace compiler {
namespace parser {

bool Parser::Impl::isExternDeclarationStart(size_t index, size_t limit) const {
  if (index >= limit) { return false; }
  if (isSoftKeyword(index, "extern"_zc)) { return true; }
  return isSoftKeyword(index, "unsafe"_zc) && index + 1 < limit &&
         isSoftKeyword(index + 1, "extern"_zc);
}

bool Parser::Impl::isSoftDeclarationHead(size_t index, size_t limit) const {
  if (index >= limit) { return false; }
  if (isSoftKeyword(index, "macro"_zc) || isExternDeclarationStart(index, limit) ||
      isSoftKeyword(index, "impl"_zc)) {
    return true;
  }
  return isSoftKeyword(index, "unsafe"_zc) && index + 1 < limit &&
         isSoftKeyword(index + 1, "impl"_zc);
}

bool Parser::Impl::parseExternAbi(size_t index, ast::Abi& abi) const {
  abi = ast::Abi::Cdecl;
  if (kindAt(index) != ast::SyntaxKind::StringLiteral) { return true; }

  const zc::StringPtr text = tokenAt(index).getValue();
  if (text == "C"_zc || text == "Cdecl"_zc) {
    abi = ast::Abi::Cdecl;
    return true;
  }
  if (text == "system"_zc) {
    abi = ast::Abi::Stdcall;
    return true;
  }
  if (text == "zom-cdecl"_zc) {
    abi = ast::Abi::ZomNative;
    return true;
  }

  diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
      tokenAt(index).getLocation());
  return false;
}

bool Parser::Impl::isMacroInvocationStart(size_t start, size_t limit) const {
  return start + 2 < limit && kindAt(start) == ast::SyntaxKind::Identifier &&
         kindAt(start + 1) == ast::SyntaxKind::Exclamation && isMacroGroupOpen(kindAt(start + 2));
}

size_t Parser::Impl::findMacroInvocationEnd(size_t start, size_t limit) const {
  if (!isMacroInvocationStart(start, limit)) { return start; }

  const size_t close = findMatchingMacroGroup(start + 2, limit);
  return close < limit ? close + 1 : limit;
}

bool Parser::Impl::isOuterAttributeStart(size_t index, size_t limit) const {
  return index + 1 < limit && kindAt(index) != ast::SyntaxKind::EndOfFile &&
         isAttributeStart(kindAt(index), kindAt(index + 1)) &&
         tokenAt(index).getRange().getEnd() == tokenAt(index + 1).getRange().getStart();
}

size_t Parser::Impl::skipOuterAttributePrefix(size_t start, size_t end) const {
  size_t cursor = start;
  while (isOuterAttributeStart(cursor, end)) {
    const size_t closeBracket = findMatchingRightBracket(cursor + 1, end);
    if (closeBracket >= end) { break; }
    cursor = closeBracket + 1;
  }
  return cursor;
}

ast::NodeId Parser::Impl::makeModulePath(AstFactory& builder, size_t start, size_t end) const {
  return builder.makeModulePath(rangeFor(start, end), makeIdentList(builder, start, end));
}

bool Parser::Impl::isModulePathSeparatorAt(size_t index, size_t end) const {
  if (index >= end) { return false; }
  if (kindAt(index) == ast::SyntaxKind::Period) { return true; }
  return kindAt(index) == ast::SyntaxKind::ColonColon;
}

size_t Parser::Impl::findModulePathEnd(size_t start, size_t end) const {
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

size_t Parser::Impl::findModuleSpecifierGroupOpen(size_t pathEnd, size_t end) const {
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

ast::NodeId Parser::Impl::makeImportSpecifier(AstFactory& builder, size_t nameIndex,
                                              size_t aliasIndex, size_t end) const {
  ast::IdentId alias;
  if (aliasIndex < end) { alias = internIdent(builder, aliasIndex); }
  return builder.makeImportSpecifier(rangeFor(nameIndex, end), internIdent(builder, nameIndex),
                                     alias);
}

ast::NodeId Parser::Impl::makeExportSpecifier(AstFactory& builder, size_t nameIndex,
                                              size_t aliasIndex, size_t end) const {
  ast::IdentId alias;
  if (aliasIndex < end) { alias = internIdent(builder, aliasIndex); }
  return builder.makeExportSpecifier(rangeFor(nameIndex, end), internIdent(builder, nameIndex),
                                     alias);
}

void Parser::Impl::recoverModuleSpecifier(TokenCursor& cursor, size_t end) const {
  while (cursor.position() < end && cursor.peek() != ast::SyntaxKind::Comma) { cursor.advance(); }
}

ast::NodeId Parser::Impl::parseImportSpecifier(AstFactory& builder, TokenCursor& cursor,
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
    diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(cursor.position()),
                                                                  ","_zc);
    recoverModuleSpecifier(cursor, end);
  }

  return makeImportSpecifier(builder, nameIndex, aliasIndex, nodeEnd);
}

ast::NodeId Parser::Impl::parseExportSpecifier(AstFactory& builder, TokenCursor& cursor,
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
    diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(cursor.position()),
                                                                  ","_zc);
    recoverModuleSpecifier(cursor, end);
  }

  return makeExportSpecifier(builder, nameIndex, aliasIndex, nodeEnd);
}

zc::Vector<ast::NodeId> Parser::Impl::parseImportSpecifierList(AstFactory& builder, size_t start,
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

zc::Vector<ast::NodeId> Parser::Impl::parseExportSpecifierList(AstFactory& builder, size_t start,
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

ast::NodeId Parser::Impl::makeAttributePath(AstFactory& builder, size_t start, size_t end) const {
  zc::Vector<ast::IdentId> segments;
  for (size_t index = start; index < end; ++index) {
    if (isAttributePathSegment(kindAt(index))) { segments.add(internIdent(builder, index)); }
  }

  return builder.makeAttributePath(rangeFor(start, end), builder.makeIdentList(segments.asPtr()),
                                   0);
}

size_t Parser::Impl::findAttributePathEnd(size_t start, size_t end) const {
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

bool Parser::Impl::isWhitelistedBareAttribute(size_t start, size_t end) const {
  if (attributePathSegmentCount(start, end) != 1) { return false; }
  zc::StringPtr text = tokenAt(start).getValue();
  if (text.size() == 0) { text = tokenLabel(tokenAt(start)); }
  return text == "inline"_zc || text == "deprecated"_zc || text == "cold"_zc || text == "repr"_zc;
}

void Parser::Impl::diagnoseImportPathSyntax(size_t clauseStart, size_t clauseEnd, size_t pathEnd,
                                            size_t groupOpen) const {
  TokenCursor periodCursor = tokenCursorAt(clauseStart);
  const size_t period = consumeBalancedUntil(periodCursor, clauseEnd, ast::SyntaxKind::Period);
  if (period < clauseEnd) {
    diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
        tokenAt(period).getLocation());
    return;
  }

  TokenCursor rangeCursor = tokenCursorAt(clauseStart);
  const size_t range = consumeBalancedUntil(rangeCursor, clauseEnd, ast::SyntaxKind::DotDotDot);
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

bool Parser::Impl::isZomCfgAttributePath(size_t start, size_t end) const {
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

void Parser::Impl::diagnoseCfgAttributeArgs(size_t start, size_t end) const {
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

ast::NodeId Parser::Impl::parseAttribute(AstFactory& builder, size_t start, size_t end) const {
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
    if (isZomCfgAttributePath(cursor, pathEnd)) { diagnoseCfgAttributeArgs(pathEnd + 1, argsEnd); }
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

  return builder.makeAttribute(rangeFor(start, end), makeAttributePath(builder, cursor, pathEnd),
                               builder.makeList(args.asPtr()));
}

ast::NodeId Parser::Impl::parseOuterAttributeList(AstFactory& builder, size_t start,
                                                  size_t end) const {
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

  return builder.makeAttributeList(rangeFor(start, cursor), builder.makeList(attrs.asPtr()));
}

bool Parser::Impl::outerAttributePrefixContainsZomCfg(size_t start, size_t end) const {
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

void Parser::Impl::diagnoseDeclarationModifierGroup(size_t start, size_t end) const {
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

bool Parser::Impl::isInterfaceElementHead(size_t index, int32_t interfaceBodyDepth) const {
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

bool Parser::Impl::isInterfaceMethodInitializer(size_t index, int32_t interfaceBodyDepth) const {
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

size_t Parser::Impl::consumeMemberBoundary(size_t start, size_t limit) const {
  const ast::SyntaxKind head = kindAt(start);
  const bool bodyBearingHead =
      head == ast::SyntaxKind::FunKeyword || head == ast::SyntaxKind::GetKeyword ||
      head == ast::SyntaxKind::SetKeyword || head == ast::SyntaxKind::InitKeyword ||
      head == ast::SyntaxKind::DeinitKeyword;
  int32_t angleDepth = 0;
  bool sawFieldColon = false;
  bool sawEquals = false;
  TokenCursor cursor = tokenCursorAt(start);
  TokenCursor::ScopedSplitMode splitMode(cursor);
  while (cursor.position() < limit) {
    const size_t index = cursor.position();
    const ast::SyntaxKind kind = cursor.peek();
    if (kind == ast::SyntaxKind::EndOfFile) { return index; }
    if (angleDepth == 0) {
      if (kind == ast::SyntaxKind::Semicolon || kind == ast::SyntaxKind::Comma) {
        return index + 1;
      }
      if (kind == ast::SyntaxKind::Colon) { sawFieldColon = true; }
      if (kind == ast::SyntaxKind::Equals) { sawEquals = true; }
      if (kind == ast::SyntaxKind::LeftParen) {
        const size_t closeParen = findMatchingRightParen(index, limit);
        cursor.moveTo(closeParen < limit ? closeParen + 1 : limit);
        continue;
      }
      if (kind == ast::SyntaxKind::LeftBracket) {
        const size_t closeBracket = findMatchingRightBracket(index, limit);
        cursor.moveTo(closeBracket < limit ? closeBracket + 1 : limit);
        continue;
      }
      if (kind == ast::SyntaxKind::LeftBrace) {
        const size_t bodyEnd = consumeBracedBodyEnd(index, limit);
        const bool fieldAccessorBody = head == ast::SyntaxKind::Identifier && sawFieldColon &&
                                       !sawEquals && index > start &&
                                       kindAt(index - 1) != ast::SyntaxKind::Colon;
        if (bodyBearingHead || fieldAccessorBody) { return bodyEnd; }
        cursor.moveTo(bodyEnd);
        continue;
      }
    }

    if (kind == ast::SyntaxKind::LessThan) {
      ++angleDepth;
    } else if (kind == ast::SyntaxKind::GreaterThan) {
      if (angleDepth > 0) { --angleDepth; }
    }
    cursor.advance();
  }
  return limit;
}

void Parser::Impl::diagnoseMissingFieldMemberSemicolon(size_t start, size_t end) const {
  if (start >= end || kindAt(start) != ast::SyntaxKind::Identifier) { return; }

  TokenCursor colonCursor = tokenCursorAt(start + 1);
  const size_t colon = consumeBalancedUntil(colonCursor, end, ast::SyntaxKind::Colon);
  if (colon >= end) { return; }

  int32_t parenDepth = 0;
  int32_t bracketDepth = 0;
  int32_t braceDepth = 0;
  int32_t angleDepth = 0;
  TokenCursor cursor = tokenCursorAt(colon + 1);
  TokenCursor::ScopedSplitMode splitMode(cursor);
  while (cursor.position() + 1 < end) {
    const size_t index = cursor.position();
    const ast::SyntaxKind kind = cursor.peek();

    if (parenDepth == 0 && bracketDepth == 0 && braceDepth == 0 && angleDepth == 0 &&
        kind == ast::SyntaxKind::Identifier && kindAt(index + 1) == ast::SyntaxKind::Colon) {
      diagnosticEngine.diagnose<diagnostics::DiagID::MissingSemicolon>(tokenAt(index).getLocation(),
                                                                       tokenLabel(tokenAt(index)));
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
      case ast::SyntaxKind::GreaterThan:
        if (angleDepth > 0) { --angleDepth; }
        break;
      default:
        break;
    }
    cursor.advance();
  }
}

void Parser::Impl::diagnoseNamedTypeBody(size_t bodyOpen, size_t bodyClose,
                                         ast::SyntaxKind kind) const {
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
    TokenCursor semiCursor = tokenCursorAt(cursor);
    const size_t semi = consumeBalancedUntil(semiCursor, memberEnd, ast::SyntaxKind::Semicolon);
    TokenCursor bodyCursor = tokenCursorAt(cursor);
    const size_t body = consumeBalancedUntil(bodyCursor, memberEnd, ast::SyntaxKind::LeftBrace);

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
      TokenCursor equalsCursor = tokenCursorAt(cursor);
      const size_t equals = consumeBalancedUntil(equalsCursor, memberEnd, ast::SyntaxKind::Equals);
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

ast::NodeId Parser::Impl::makeEmptyMacroTokenTree(AstFactory& builder, size_t start,
                                                  size_t end) const {
  zc::Vector<ast::NodeId> tokens;
  return builder.makeMacroTokenTree(rangeFor(start, end), builder.makeList(tokens.asPtr()));
}

ast::NodeId Parser::Impl::makeEmptyClassMemberList(AstFactory& builder,
                                                   source::SourceRange range) const {
  zc::Vector<ast::NodeId> members;
  return builder.makeClassMemberList(zc::mv(range), static_cast<uint16_t>(members.size()),
                                     builder.makeList(members.asPtr()));
}

ast::NodeId Parser::Impl::makeEmptyEnumVariantList(AstFactory& builder,
                                                   source::SourceRange range) const {
  zc::Vector<ast::NodeId> variants;
  return builder.makeEnumVariantList(zc::mv(range), static_cast<uint16_t>(variants.size()),
                                     builder.makeList(variants.asPtr()));
}

size_t Parser::Impl::recoverFunctionParameter(TokenCursor& cursor, size_t closeParen) const {
  while (cursor.position() < closeParen && cursor.peek() != ast::SyntaxKind::Comma) {
    cursor.advance();
  }
  return cursor.position();
}

ast::NodeId Parser::Impl::parseFunctionParameter(AstFactory& builder, TokenCursor& cursor,
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
    diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(cursor.position()),
                                                                  ":"_zc);
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

  ast::NodeId defaultValue;

  if (cursor.position() < closeParen && cursor.peek() == ast::SyntaxKind::Equals) {
    cursor.advance();
    const size_t defaultStart = cursor.position();
    const size_t defaultEnd = consumeVariableInitializer(cursor, closeParen);
    if (defaultStart >= defaultEnd) {
      diagnoseExpressionExpected(defaultStart);
      return ast::NodeId();
    }
    defaultValue = parseRequiredExpression(builder, defaultStart, defaultEnd);
  }

  return builder.makeFunctionParameterDecl(rangeFor(parameterStart, cursor.position()), name,
                                           ty.node, defaultValue, ast::NodeId());
}

ast::NodeList Parser::Impl::parseFunctionParameterNodeList(AstFactory& builder, size_t openParen,
                                                           size_t closeParen) const {
  zc::Vector<ast::NodeId> parameters;
  if (openParen < closeParen && !isAtEnd(closeParen)) {
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

ast::NodeId Parser::Impl::parseFunctionParameterList(AstFactory& builder, size_t openParen,
                                                     size_t closeParen) const {
  const ast::NodeList parameterList =
      parseFunctionParameterNodeList(builder, openParen, closeParen);
  return builder.makeFunctionParameterList(rangeFor(openParen, closeParen + 1),
                                           static_cast<uint16_t>(parameterList.size),
                                           parameterList);
}

size_t Parser::Impl::consumeExternDeclarationEnd(size_t start, size_t limit) const {
  RecoveryFrameScope recoveryFrame(*this, RecoveryContext::Declaration, start);
  size_t cursor = start;
  if (isSoftKeyword(cursor, "unsafe"_zc)) { ++cursor; }
  if (cursor < limit && isSoftKeyword(cursor, "extern"_zc)) { ++cursor; }
  if (cursor < limit && kindAt(cursor) == ast::SyntaxKind::StringLiteral) { ++cursor; }

  if (cursor < limit && kindAt(cursor) == ast::SyntaxKind::LeftBrace) {
    return recoveryFrame.finish(consumeBracedBodyEnd(cursor, limit));
  }

  return recoveryFrame.finish(consumeSimpleStatementEnd(start, limit));
}

size_t Parser::Impl::findBindingDeclarationRecoveryStart(size_t start, size_t limit) const {
  int32_t parenDepth = 0;
  int32_t bracketDepth = 0;
  int32_t braceDepth = 0;
  int32_t angleDepth = 0;
  bool inTypeAnnotation = false;
  bool inInitializer = false;
  TokenCursor cursor = tokenCursorAt(start + 1);
  TokenCursor::ScopedSplitMode splitMode(cursor);
  while (cursor.position() < limit) {
    const size_t index = cursor.position();
    const ast::SyntaxKind kind = cursor.peek();
    if (kind == ast::SyntaxKind::EndOfFile) { return index; }
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
      case ast::SyntaxKind::GreaterThan:
        if (inTypeAnnotation && angleDepth > 0) { --angleDepth; }
        break;
      default:
        break;
    }
    cursor.advance();
  }
  return limit;
}

size_t Parser::Impl::consumeBindingDeclarationEnd(size_t start, size_t limit) const {
  RecoveryFrameScope recoveryFrame(*this, RecoveryContext::Declaration, start);
  TokenCursor semicolonCursor = tokenCursorAt(start + 1);
  const size_t semicolon = consumeBalancedUntil(semicolonCursor, limit, ast::SyntaxKind::Semicolon);
  if (semicolon < limit) { return recoveryFrame.finish(semicolon + 1); }

  const size_t recoveryStart = findBindingDeclarationRecoveryStart(start, limit);
  if (recoveryStart < limit) {
    diagnosticEngine.diagnose<diagnostics::DiagID::MissingSemicolon>(
        tokenAt(recoveryStart).getLocation(), tokenLabel(tokenAt(recoveryStart)));
    return recoveryFrame.finish(recoveryStart);
  }
  return recoveryFrame.finish(consumeSimpleStatementEnd(start, limit));
}

Parser::Impl::FunctionDeclarationParts Parser::Impl::parseFunctionDeclarationParts(
    size_t start, size_t limit) const {
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
    TokenCursor angleCursor = tokenCursorAt(cursor);
    cursor = consumeBalancedAngleList(angleCursor, limit) ? angleCursor.position() : limit;
  }
  if (cursor < limit && kindAt(cursor) == ast::SyntaxKind::LeftParen) {
    parts.openParen = cursor;
  } else {
    TokenCursor openParenCursor = tokenCursorAt(cursor);
    parts.openParen = consumeBalancedUntil(openParenCursor, limit, ast::SyntaxKind::LeftParen);
  }
  if (parts.openParen >= limit) {
    parts.end = consumeSimpleStatementEnd(start, limit);
    return parts;
  }

  parts.closeParen = findMatchingRightParen(parts.openParen, limit);
  if (parts.closeParen >= limit) { return parts; }

  parts.bodyOpen = findFunctionBodyOpenAfterParams(parts.closeParen, limit);
  parts.headerEnd = parts.bodyOpen < limit ? parts.bodyOpen : limit;
  TokenCursor semiCursor = tokenCursorAt(parts.closeParen + 1);
  const size_t semi = consumeBalancedUntil(semiCursor, parts.headerEnd, ast::SyntaxKind::Semicolon);
  if (semi < parts.headerEnd) {
    parts.headerEnd = semi;
    parts.end = semi + 1;
  } else {
    parts.end = parts.bodyOpen < limit ? consumeBracedBodyEnd(parts.bodyOpen, limit) : limit;
  }
  TokenCursor arrowCursor = tokenCursorAt(parts.closeParen + 1);
  parts.arrow = consumeBalancedTypeUntil(arrowCursor, parts.headerEnd, ast::SyntaxKind::Arrow);
  TokenCursor raisesCursor = tokenCursorAt(parts.closeParen + 1);
  parts.raises =
      consumeBalancedTypeUntil(raisesCursor, parts.headerEnd, ast::SyntaxKind::RaisesKeyword);
  return parts;
}

size_t Parser::Impl::consumeFunctionDeclarationEnd(size_t start, size_t limit) const {
  RecoveryFrameScope recoveryFrame(*this, RecoveryContext::Declaration, start);
  return recoveryFrame.finish(parseFunctionDeclarationParts(start, limit).end);
}

size_t Parser::Impl::consumeExportDeclarationEnd(size_t start, size_t limit) const {
  RecoveryFrameScope recoveryFrame(*this, RecoveryContext::Declaration, start);
  size_t declarationHead = start + 1;
  while (declarationHead < limit && kindAt(declarationHead) != ast::SyntaxKind::EndOfFile &&
         isDeclarationModifier(kindAt(declarationHead))) {
    ++declarationHead;
  }
  if (declarationHead < limit && (isDeclarationHead(kindAt(declarationHead)) ||
                                  isSoftDeclarationHead(declarationHead, limit))) {
    TokenCursor cursor = tokenCursorAt(declarationHead);
    return recoveryFrame.finish(consumeSourceElement(cursor, limit).end);
  }
  return recoveryFrame.finish(consumeSimpleStatementEnd(start, limit));
}

size_t Parser::Impl::consumeNamedTypeDeclarationEnd(size_t start, size_t limit) const {
  RecoveryFrameScope recoveryFrame(*this, RecoveryContext::Declaration, start);
  size_t cursor = start + 1;
  while (cursor < limit && kindAt(cursor) != ast::SyntaxKind::EndOfFile &&
         kindAt(cursor) != ast::SyntaxKind::Identifier) {
    ++cursor;
  }
  if (cursor < limit && kindAt(cursor) != ast::SyntaxKind::EndOfFile) { ++cursor; }

  if (cursor < limit && kindAt(cursor) == ast::SyntaxKind::LessThan) {
    TokenCursor angleCursor = tokenCursorAt(cursor);
    cursor = consumeBalancedAngleList(angleCursor, limit) ? angleCursor.position() : limit;
  }

  while (cursor < limit) {
    const ast::SyntaxKind kind = kindAt(cursor);
    if (kind == ast::SyntaxKind::EndOfFile) { return recoveryFrame.finish(cursor); }
    if (kind == ast::SyntaxKind::LeftBrace) {
      return recoveryFrame.finish(consumeBracedBodyEnd(cursor, limit));
    }
    if (kind == ast::SyntaxKind::Semicolon) { return recoveryFrame.finish(cursor + 1); }

    if (kind == ast::SyntaxKind::ExtendsKeyword || kind == ast::SyntaxKind::ImplementsKeyword) {
      ++cursor;
      while (cursor < limit) {
        if (kindAt(cursor) == ast::SyntaxKind::EndOfFile) { return recoveryFrame.finish(cursor); }
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

  return recoveryFrame.finish(limit);
}

size_t Parser::Impl::consumeBracedDeclarationEnd(size_t start, size_t limit) const {
  RecoveryFrameScope recoveryFrame(*this, RecoveryContext::Declaration, start);
  TokenCursor bodyCursor = tokenCursorAt(start + 1);
  const size_t bodyOpen = consumeBalancedUntil(bodyCursor, limit, ast::SyntaxKind::LeftBrace);
  TokenCursor semiCursor = tokenCursorAt(start + 1);
  const size_t semi = consumeBalancedUntil(semiCursor, limit, ast::SyntaxKind::Semicolon);
  if (bodyOpen < limit && (semi >= limit || bodyOpen < semi)) {
    return recoveryFrame.finish(consumeBracedBodyEnd(bodyOpen, limit));
  }
  return recoveryFrame.finish(semi < limit ? semi + 1 : limit);
}

ast::NodeId Parser::Impl::parseModuleDeclaration(AstFactory& builder, size_t start,
                                                 size_t end) const {
  size_t pathEnd = end;
  TokenCursor semicolonCursor = tokenCursorAt(start + 1);
  const size_t semicolon = consumeBalancedUntil(semicolonCursor, end, ast::SyntaxKind::Semicolon);
  TokenCursor equalsCursor = tokenCursorAt(start + 1);
  const size_t equals = consumeBalancedUntil(equalsCursor, end, ast::SyntaxKind::Equals);
  TokenCursor bodyCursor = tokenCursorAt(start + 1);
  const size_t bodyOpen = consumeBalancedUntil(bodyCursor, end, ast::SyntaxKind::LeftBrace);
  if (semicolon < pathEnd) { pathEnd = semicolon; }
  if (equals < pathEnd) { pathEnd = equals; }
  if (bodyOpen < pathEnd) { pathEnd = bodyOpen; }

  const bool simpleModuleName = semicolon < end && equals >= semicolon && bodyOpen >= end;
  if (simpleModuleName) {
    TokenCursor colonColonCursor = tokenCursorAt(start + 1);
    const size_t colonColon =
        consumeBalancedUntil(colonColonCursor, pathEnd, ast::SyntaxKind::ColonColon);
    if (colonColon < pathEnd) {
      diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
          tokenAt(colonColon).getLocation());
      return ast::NodeId();
    }
  }

  return builder.makeModuleDeclaration(rangeFor(start, end),
                                       makeModulePath(builder, start + 1, pathEnd));
}

ast::NodeId Parser::Impl::parseImportDeclaration(AstFactory& builder, size_t start,
                                                 size_t end) const {
  const size_t clauseStart = start + 1;
  const size_t clauseEnd =
      end > clauseStart && kindAt(end - 1) == ast::SyntaxKind::Semicolon ? end - 1 : end;
  const size_t pathEnd = findModulePathEnd(clauseStart, clauseEnd);
  const size_t groupOpen = findModuleSpecifierGroupOpen(pathEnd, clauseEnd);
  zc::Vector<ast::NodeId> specifiers;
  ast::IdentId alias;
  if (pathEnd <= clauseStart) {
    diagnosticEngine.diagnose<diagnostics::DiagID::IdentifierExpected>(diagnosticLoc(clauseStart));
  }
  diagnoseImportPathSyntax(clauseStart, clauseEnd, pathEnd, groupOpen);
  if (groupOpen < clauseEnd) {
    const size_t closeBrace = findMatchingRightBrace(groupOpen, clauseEnd);
    specifiers = parseImportSpecifierList(builder, groupOpen + 1,
                                          closeBrace < clauseEnd ? closeBrace : clauseEnd);
  } else if (pathEnd < clauseEnd && kindAt(pathEnd) == ast::SyntaxKind::AsKeyword) {
    const size_t aliasIndex = pathEnd + 1;
    if (aliasIndex < clauseEnd && kindAt(aliasIndex) == ast::SyntaxKind::Identifier) {
      alias = internIdent(builder, aliasIndex);
    } else {
      diagnosticEngine.diagnose<diagnostics::DiagID::IdentifierExpected>(diagnosticLoc(aliasIndex));
    }
  }
  return builder.makeImportDeclaration(rangeFor(start, end),
                                       makeModulePath(builder, clauseStart, pathEnd), alias,
                                       builder.makeList(specifiers.asPtr()));
}

ast::NodeId Parser::Impl::parseExportDeclaration(AstFactory& builder, size_t start,
                                                 size_t end) const {
  const size_t clauseStart = start + 1;
  const size_t clauseEnd =
      end > clauseStart && kindAt(end - 1) == ast::SyntaxKind::Semicolon ? end - 1 : end;
  zc::Vector<ast::NodeId> specifiers;
  ast::NodeId declarationNode;
  ast::NodeId pathNode;
  size_t declarationHead = clauseStart;
  while (declarationHead < clauseEnd && isDeclarationModifier(kindAt(declarationHead))) {
    ++declarationHead;
  }
  if (declarationHead < clauseEnd && (isDeclarationHead(kindAt(declarationHead)) ||
                                      isSoftDeclarationHead(declarationHead, clauseEnd))) {
    TokenCursor cursor = tokenCursorAt(clauseStart);
    const SourceElementParseResult declaration = parseSourceElement(builder, cursor, end);
    declarationNode = declaration.node;
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
      pathNode = makeModulePath(builder, clauseStart, pathEnd);
      const size_t closeBrace = findMatchingRightBrace(groupOpen, clauseEnd);
      specifiers = parseExportSpecifierList(builder, groupOpen + 1,
                                            closeBrace < clauseEnd ? closeBrace : clauseEnd);
    } else if (kindAt(clauseStart) != ast::SyntaxKind::DefaultKeyword) {
      diagnosticEngine.diagnose<diagnostics::DiagID::DeclarationExpected>(
          diagnosticLoc(clauseStart));
    }
  }
  return builder.makeExportDeclaration(rangeFor(start, end), declarationNode, pathNode,
                                       builder.makeList(specifiers.asPtr()));
}

size_t Parser::Impl::consumeVariableInitializer(TokenCursor& cursor, size_t limit) const {
  while (cursor.position() < limit) {
    const ast::SyntaxKind kind = cursor.peek();
    if (kind == ast::SyntaxKind::EndOfFile) { return cursor.position(); }
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
    if (kind == ast::SyntaxKind::LessThan && isInitializerGenericAngle(cursor.position(), limit)) {
      const size_t closeAngle = findMatchingAngleClose(cursor.position(), limit);
      cursor.moveTo(closeAngle + 1);
      continue;
    }

    cursor.advance();
  }

  return cursor.position();
}

Parser::Impl::VariableDeclaratorParseResult Parser::Impl::parseVariableDeclarator(
    AstFactory& builder, TokenCursor& cursor, size_t limit) const {
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

  ast::NodeId tyNode;
  ast::NodeId initNode;

  if (cursor.position() < limit && cursor.peek() == ast::SyntaxKind::Colon) {
    cursor.advance();
    const size_t typeStart = cursor.position();
    TypeParseResult ty = parseTypeExpression(builder, cursor, limit);
    if (!ty.node) {
      diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(typeStart));
      return VariableDeclaratorParseResult();
    }
    tyNode = ty.node;
  }

  if (cursor.position() < limit && cursor.peek() == ast::SyntaxKind::Equals) {
    cursor.advance();
    const size_t initStart = cursor.position();
    const size_t initEnd = consumeVariableInitializer(cursor, limit);
    if (initStart >= initEnd) {
      diagnoseExpressionExpected(initStart);
      return VariableDeclaratorParseResult();
    }

    initNode = parseRequiredExpression(builder, initStart, initEnd);
    if (!initNode) { return VariableDeclaratorParseResult(); }
  }

  return {
      builder.makeVariableDeclarator(rangeFor(start, cursor.position()), pattern, tyNode, initNode),
      cursor.position()};
}

ast::NodeId Parser::Impl::parseVariableDeclaratorList(AstFactory& builder, size_t start,
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
    const VariableDeclaratorParseResult declarator = parseVariableDeclarator(builder, cursor, end);
    if (!declarator.node) { return ast::NodeId(); }
    declarators.add(declarator.node);

    if (cursor.position() <= declaratorStart) { cursor.moveTo(declaratorStart + 1); }
    if (cursor.position() >= end) { break; }
    if (cursor.peek() == ast::SyntaxKind::Comma) {
      cursor.advance();
      continue;
    }

    diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(cursor.position()),
                                                                  ","_zc);
    return ast::NodeId();
  }

  if (declarators.size() == 0) {
    diagnosticEngine.diagnose<diagnostics::DiagID::VariableDeclarationExpected>(
        diagnosticLoc(start));
    return ast::NodeId();
  }

  return builder.makeVariableDeclaratorList(rangeFor(start, end),
                                            static_cast<uint16_t>(declarators.size()),
                                            builder.makeList(declarators.asPtr()));
}

ast::NodeId Parser::Impl::parseExternFunctionDecl(AstFactory& builder, size_t start, size_t end,
                                                  ast::Abi abi) const {
  if (end <= start || kindAt(end - 1) != ast::SyntaxKind::Semicolon) {
    diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(end), ";"_zc);
    return ast::NodeId();
  }

  const FunctionDeclarationParts parts = parseFunctionDeclarationParts(start, end);
  if (parts.nameIndex >= end || parts.openParen >= end || parts.closeParen >= end) {
    diagnosticEngine.diagnose<diagnostics::DiagID::IdentifierExpected>(diagnosticLoc(start + 1));
    return ast::NodeId();
  }

  ast::NodeId retTy;
  ast::NodeId raisesTy;
  if (parts.arrow < parts.headerEnd) {
    const size_t retEnd = parts.raises < parts.headerEnd ? parts.raises : parts.headerEnd;
    retTy = parseTypeRange(builder, parts.arrow + 1, retEnd);
  }
  if (parts.raises < parts.headerEnd) {
    raisesTy = parseTypeRange(builder, parts.raises + 1, parts.headerEnd);
  }
  return builder.makeExternDecl(
      rangeFor(start, end), internIdent(builder, parts.nameIndex), abi,
      parseFunctionParameterNodeList(builder, parts.openParen, parts.closeParen), retTy, raisesTy);
}

ast::NodeId Parser::Impl::parseExternVarDecl(AstFactory& builder, size_t start, size_t end,
                                             ast::Abi abi) const {
  const size_t nameIndex = start + 1;
  TokenCursor colonCursor = tokenCursorAt(nameIndex + 1);
  const size_t colon = consumeBalancedUntil(colonCursor, end, ast::SyntaxKind::Colon);
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
  return builder.makeExternVarDecl(rangeFor(start, end), internIdent(builder, nameIndex),
                                   parseTypeRange(builder, colon + 1, typeEnd), abi, true);
}

ast::NodeId Parser::Impl::parseMacroRulesDeclaration(AstFactory& builder, size_t start,
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
    diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(bangIndex), "!"_zc);
    return ast::NodeId();
  }

  const size_t bodyOpen = bangIndex + 1;
  if (bodyOpen >= end || kindAt(bodyOpen) != ast::SyntaxKind::LeftBrace) {
    diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(bodyOpen), "{"_zc);
    return ast::NodeId();
  }

  const size_t bodyClose = findMatchingRightBrace(bodyOpen, end);
  if (bodyClose >= end) {
    diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(bodyOpen), "}"_zc);
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

    TokenCursor arrowCursor = tokenCursorAt(patternClose + 1);
    const size_t arrow =
        consumeBalancedUntil(arrowCursor, bodyClose, ast::SyntaxKind::EqualsGreaterThan);
    if (arrow >= bodyClose) {
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(patternClose + 1),
                                                                    "=>"_zc);
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
      TokenCursor semicolonCursor = tokenCursorAt(expansionStart);
      const size_t semicolon =
          consumeBalancedUntil(semicolonCursor, bodyClose, ast::SyntaxKind::Semicolon);
      expansionEnd = semicolon < bodyClose ? semicolon : bodyClose;
    }

    rules.add(
        builder.makeMacroRule(rangeFor(patternStart, expansionEnd),
                              makeEmptyMacroPattern(builder, patternStart, patternClose + 1),
                              makeEmptyMacroTokenTree(builder, expansionStart, expansionEnd)));

    cursor = expansionEnd;
    if (cursor < bodyClose && kindAt(cursor) == ast::SyntaxKind::Semicolon) { ++cursor; }
  }

  return builder.makeMacroRulesDecl(rangeFor(start, end), internIdent(builder, nameIndex),
                                    builder.makeList(rules.asPtr()));
}

ast::NodeId Parser::Impl::parseFunctionDeclaration(AstFactory& builder, size_t start,
                                                   size_t end) const {
  const FunctionDeclarationParts parts = parseFunctionDeclarationParts(start, end);

  if (parts.nameIndex < end) { diagnoseDeclarationTypeParameterSyntax(parts.nameIndex + 1, end); }

  const size_t whereSearchStart =
      parts.closeParen < parts.headerEnd ? parts.closeParen + 1 : parts.headerEnd;
  TokenCursor whereCursor = tokenCursorAt(whereSearchStart);
  const size_t where = consumeBalancedTypeIdentifierUntil(whereCursor, parts.headerEnd, "where"_zc);
  if (where < parts.headerEnd) {
    diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
        tokenAt(where).getLocation());
    return ast::NodeId();
  }

  ast::IdentId name;
  if (parts.nameIndex < end) { name = internIdent(builder, parts.nameIndex); }
  const ast::NodeId params = parseFunctionParameterList(builder, parts.openParen, parts.closeParen);
  ast::NodeId retTy;
  if (parts.arrow < end) {
    const size_t retEnd = parts.raises < parts.headerEnd ? parts.raises : parts.headerEnd;
    retTy = parseTypeRange(builder, parts.arrow + 1, retEnd);
  }
  ast::NodeId raisesTy;
  if (parts.raises < parts.headerEnd) {
    if (parts.raises + 1 >= parts.headerEnd || kindAt(parts.raises + 1) == ast::SyntaxKind::Arrow) {
      diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(parts.raises + 1));
      return ast::NodeId();
    }
    raisesTy = parseTypeRange(builder, parts.raises + 1, parts.headerEnd);
  }
  return builder.makeFunctionDecl(rangeFor(start, end), name, params, ast::NodeId(), retTy,
                                  raisesTy, parseBlock(builder, parts.bodyOpen, end));
}

ast::NodeId Parser::Impl::parseNamedTypeDeclaration(AstFactory& builder, size_t start, size_t end,
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
    TokenCursor angleCursor = tokenCursorAt(headerCursor);
    headerCursor = consumeBalancedAngleList(angleCursor, end) ? angleCursor.position() : end;
  }
  if (kind == ast::SyntaxKind::StructDecl && headerCursor < end &&
      kindAt(headerCursor) == ast::SyntaxKind::LeftParen) {
    diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
        tokenAt(headerCursor).getLocation());
    return ast::NodeId();
  }

  TokenCursor bodyCursor = tokenCursorAt(headerCursor);
  const size_t bodyOpen = consumeBalancedUntil(bodyCursor, end, ast::SyntaxKind::LeftBrace);
  const size_t headerEnd = bodyOpen < end ? bodyOpen : end;
  TokenCursor whereCursor = tokenCursorAt(headerCursor);
  const size_t where = consumeBalancedTypeIdentifierUntil(whereCursor, headerEnd, "where"_zc);
  if (where < headerEnd) {
    diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
        tokenAt(where).getLocation());
    return ast::NodeId();
  }

  if (bodyOpen < end) {
    const size_t bodyClose = findMatchingRightBrace(bodyOpen, end);
    if (bodyClose < end) { diagnoseNamedTypeBody(bodyOpen, bodyClose, kind); }
  }

  if (kind == ast::SyntaxKind::ClassDecl) {
    ast::IdentId name;
    if (nameIndex < end) { name = internIdent(builder, nameIndex); }
    return builder.makeClassDecl(rangeFor(start, end), name, ast::ClassExtensibility::Sealed,
                                 ast::NodeId(), ast::NodeId(),
                                 makeEmptyClassMemberList(builder, rangeFor(start, end)));
  } else if (kind == ast::SyntaxKind::StructDecl) {
    ast::IdentId name;
    if (nameIndex < end) { name = internIdent(builder, nameIndex); }
    return builder.makeStructDecl(rangeFor(start, end), name, ast::ClassExtensibility::Sealed,
                                  ast::NodeId(),
                                  makeEmptyClassMemberList(builder, rangeFor(start, end)));
  } else if (kind == ast::SyntaxKind::InterfaceDecl) {
    ast::IdentId name;
    if (nameIndex < end) { name = internIdent(builder, nameIndex); }
    return builder.makeInterfaceDecl(rangeFor(start, end), name, ast::NodeId(),
                                     makeEmptyClassMemberList(builder, rangeFor(start, end)));
  } else if (kind == ast::SyntaxKind::EnumDeclaration) {
    ast::IdentId name;
    if (nameIndex < end) { name = internIdent(builder, nameIndex); }
    return builder.makeEnumDeclaration(rangeFor(start, end), name, ast::ClassExtensibility::Sealed,
                                       ast::NodeId(), 0,
                                       makeEmptyEnumVariantList(builder, rangeFor(start, end)), 0);
  }
  return ast::NodeId();
}

ast::NodeId Parser::Impl::parseErrorDeclaration(AstFactory& builder, size_t start,
                                                size_t end) const {
  size_t nameIndex = end;
  for (size_t index = start + 1; index < end; ++index) {
    if (kindAt(index) == ast::SyntaxKind::Identifier) {
      nameIndex = index;
      break;
    }
  }

  ast::IdentId name;
  if (nameIndex < end) { name = internIdent(builder, nameIndex); }
  return builder.makeErrorDecl(rangeFor(start, end), name,
                               makeEmptyClassMemberList(builder, rangeFor(start, end)));
}

ast::NodeId Parser::Impl::parseAliasDeclaration(AstFactory& builder, size_t start,
                                                size_t end) const {
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

  TokenCursor equalsCursor = tokenCursorAt(start + 1);
  const size_t equals = consumeBalancedUntil(equalsCursor, end, ast::SyntaxKind::Equals);
  ast::IdentId name;
  if (nameIndex < end) { name = internIdent(builder, nameIndex); }
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
    return builder.makeAliasDecl(rangeFor(start, end), name, ast::NodeId(), target);
  } else {
    diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(nameIndex + 1),
                                                                  "="_zc);
    return ast::NodeId();
  }
}

ast::NodeId Parser::Impl::parseImplInterfaceBound(AstFactory& builder, size_t start,
                                                  size_t end) const {
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
      diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(pathEnd), ">"_zc);
      return ast::NodeId();
    }
    boundEnd = end;
  }

  if (boundEnd != end) {
    TokenCursor barCursor = tokenCursorAt(start);
    const size_t bar = consumeBalancedTypeUntil(barCursor, end, ast::SyntaxKind::Bar);
    TokenCursor ampersandCursor = tokenCursorAt(start);
    const size_t ampersand =
        consumeBalancedTypeUntil(ampersandCursor, end, ast::SyntaxKind::Ampersand);
    TokenCursor commaCursor = tokenCursorAt(start);
    const size_t comma = consumeBalancedTypeUntil(commaCursor, end, ast::SyntaxKind::Comma);
    size_t location = boundEnd;
    if (bar < end) {
      location = bar;
    } else if (ampersand < end) {
      location = ampersand;
    } else if (comma < end) {
      location = comma;
    }
    diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(location), "+"_zc);
    return ast::NodeId();
  }

  const ast::NodeId iface = parseTypeRange(builder, start, end);
  if (!iface) {
    diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(start));
  }
  return iface;
}

ast::NodeId Parser::Impl::parseStandaloneImplDeclaration(AstFactory& builder, size_t start,
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

  TokenCursor bodyCursor = tokenCursorAt(implIndex + 1);
  const size_t bodyOpen = consumeBalancedUntil(bodyCursor, end, ast::SyntaxKind::LeftBrace);
  TokenCursor semiCursor = tokenCursorAt(implIndex + 1);
  const size_t semi = consumeBalancedUntil(semiCursor, end, ast::SyntaxKind::Semicolon);
  const size_t headerEnd = bodyOpen < end ? bodyOpen : (semi < end ? semi : end);
  TokenCursor forCursor = tokenCursorAt(implIndex + 1);
  const size_t forIndex =
      consumeBalancedTypeUntil(forCursor, headerEnd, ast::SyntaxKind::ForKeyword);
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
    diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(diagnosticLoc(headerEnd), "{"_zc);
    return ast::NodeId();
  }

  const ast::NodeId ifaces = makeImplIfaceList(builder, implIndex + 1, forIndex);
  if (!ifaces) { return ast::NodeId(); }
  const ast::NodeId forTy = parseTypeRange(builder, forIndex + 1, headerEnd);
  if (!forTy) {
    diagnosticEngine.diagnose<diagnostics::DiagID::TypeExpected>(diagnosticLoc(forIndex + 1));
    return ast::NodeId();
  }

  ast::NodeId members;
  if (bodyOpen < end) { members = makeEmptyClassMemberList(builder, rangeFor(bodyOpen, end)); }
  return builder.makeStandaloneImplDecl(rangeFor(start, end), isUnsafe, ifaces, forTy,
                                        ast::NodeId(), ast::NodeId(), members);
}

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
