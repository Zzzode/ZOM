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

// Recovery and boundary utilities.

#include "zomlang/compiler/parser/parser-impl.h"

namespace zomlang {
namespace compiler {
namespace parser {

Parser::Impl::RecoveryFrameScope::RecoveryFrameScope(const Impl& parser, RecoveryContext context,
                                                     size_t anchor)
    : parser(parser) {
  parser.pushRecoveryFrame(context, anchor);
}

Parser::Impl::RecoveryFrameScope::~RecoveryFrameScope() { parser.popRecoveryFrame(); }

size_t Parser::Impl::RecoveryFrameScope::finish(size_t position) const {
  parser.markRecoveryConsumed(position);
  return position;
}

Parser::Impl::RecoveryFrame Parser::Impl::makeRecoveryFrame(RecoveryContext context,
                                                            size_t anchor) const {
  RecoveryFrame frame;
  frame.context = context;
  frame.anchor = anchor;
  frame.suppressedUntil = anchor;

  const auto addSync = [](RecoveryFrame& frame, ast::SyntaxKind kind) {
    if (frame.syncCount >= sizeof(frame.syncSet) / sizeof(frame.syncSet[0])) { return; }
    frame.syncSet[frame.syncCount++] = kind;
  };

  switch (context) {
    case RecoveryContext::SourceFile:
      addSync(frame, ast::SyntaxKind::ModuleKeyword);
      addSync(frame, ast::SyntaxKind::ImportKeyword);
      addSync(frame, ast::SyntaxKind::ExportKeyword);
      addSync(frame, ast::SyntaxKind::LetKeyword);
      addSync(frame, ast::SyntaxKind::ConstKeyword);
      addSync(frame, ast::SyntaxKind::MutKeyword);
      addSync(frame, ast::SyntaxKind::FunKeyword);
      addSync(frame, ast::SyntaxKind::ClassKeyword);
      addSync(frame, ast::SyntaxKind::StructKeyword);
      addSync(frame, ast::SyntaxKind::InterfaceKeyword);
      addSync(frame, ast::SyntaxKind::EnumKeyword);
      addSync(frame, ast::SyntaxKind::ErrorKeyword);
      addSync(frame, ast::SyntaxKind::TypeKeyword);
      addSync(frame, ast::SyntaxKind::AliasKeyword);
      addSync(frame, ast::SyntaxKind::EndOfFile);
      break;
    case RecoveryContext::Declaration:
      addSync(frame, ast::SyntaxKind::Semicolon);
      addSync(frame, ast::SyntaxKind::RightBrace);
      addSync(frame, ast::SyntaxKind::ModuleKeyword);
      addSync(frame, ast::SyntaxKind::ImportKeyword);
      addSync(frame, ast::SyntaxKind::ExportKeyword);
      addSync(frame, ast::SyntaxKind::LetKeyword);
      addSync(frame, ast::SyntaxKind::ConstKeyword);
      addSync(frame, ast::SyntaxKind::MutKeyword);
      addSync(frame, ast::SyntaxKind::FunKeyword);
      addSync(frame, ast::SyntaxKind::ClassKeyword);
      addSync(frame, ast::SyntaxKind::StructKeyword);
      addSync(frame, ast::SyntaxKind::InterfaceKeyword);
      addSync(frame, ast::SyntaxKind::EnumKeyword);
      addSync(frame, ast::SyntaxKind::ErrorKeyword);
      addSync(frame, ast::SyntaxKind::TypeKeyword);
      addSync(frame, ast::SyntaxKind::AliasKeyword);
      addSync(frame, ast::SyntaxKind::Identifier);
      addSync(frame, ast::SyntaxKind::EndOfFile);
      break;
    case RecoveryContext::Statement:
      addSync(frame, ast::SyntaxKind::Semicolon);
      addSync(frame, ast::SyntaxKind::RightBrace);
      addSync(frame, ast::SyntaxKind::IfKeyword);
      addSync(frame, ast::SyntaxKind::MatchKeyword);
      addSync(frame, ast::SyntaxKind::WhileKeyword);
      addSync(frame, ast::SyntaxKind::DoKeyword);
      addSync(frame, ast::SyntaxKind::ForKeyword);
      addSync(frame, ast::SyntaxKind::BreakKeyword);
      addSync(frame, ast::SyntaxKind::ContinueKeyword);
      addSync(frame, ast::SyntaxKind::ReturnKeyword);
      addSync(frame, ast::SyntaxKind::DebuggerKeyword);
      addSync(frame, ast::SyntaxKind::SuspendKeyword);
      addSync(frame, ast::SyntaxKind::SpawnKeyword);
      addSync(frame, ast::SyntaxKind::LetKeyword);
      addSync(frame, ast::SyntaxKind::ConstKeyword);
      addSync(frame, ast::SyntaxKind::FunKeyword);
      addSync(frame, ast::SyntaxKind::EndOfFile);
      break;
    case RecoveryContext::Expression:
      addSync(frame, ast::SyntaxKind::Comma);
      addSync(frame, ast::SyntaxKind::Semicolon);
      addSync(frame, ast::SyntaxKind::RightParen);
      addSync(frame, ast::SyntaxKind::RightBracket);
      addSync(frame, ast::SyntaxKind::RightBrace);
      addSync(frame, ast::SyntaxKind::Colon);
      addSync(frame, ast::SyntaxKind::EndOfFile);
      break;
    case RecoveryContext::Type:
      addSync(frame, ast::SyntaxKind::Comma);
      addSync(frame, ast::SyntaxKind::Semicolon);
      addSync(frame, ast::SyntaxKind::RightParen);
      addSync(frame, ast::SyntaxKind::RightBracket);
      addSync(frame, ast::SyntaxKind::RightBrace);
      addSync(frame, ast::SyntaxKind::Equals);
      addSync(frame, ast::SyntaxKind::Arrow);
      addSync(frame, ast::SyntaxKind::EndOfFile);
      break;
    case RecoveryContext::Pattern:
      addSync(frame, ast::SyntaxKind::Comma);
      addSync(frame, ast::SyntaxKind::RightParen);
      addSync(frame, ast::SyntaxKind::RightBracket);
      addSync(frame, ast::SyntaxKind::RightBrace);
      addSync(frame, ast::SyntaxKind::EqualsGreaterThan);
      addSync(frame, ast::SyntaxKind::IfKeyword);
      addSync(frame, ast::SyntaxKind::EndOfFile);
      break;
  }

  return frame;
}

void Parser::Impl::pushRecoveryFrame(RecoveryContext context, size_t anchor) const {
  recoveryFrames.add(makeRecoveryFrame(context, anchor));
}

void Parser::Impl::popRecoveryFrame() const {
  ZC_IREQUIRE(recoveryFrames.size() != 0, "parser recovery frame stack underflow");
  recoveryFrames.removeLast();
}

void Parser::Impl::markRecoveryConsumed(size_t position) const {
  if (recoveryFrames.size() == 0) { return; }
  RecoveryFrame& frame = recoveryFrames.back();
  if (position > frame.anchor) {
    frame.consumed = true;
    frame.suppressedUntil = position;
  }
}

bool Parser::Impl::shouldSuppressDiagnostic(size_t tokenIndex) const {
  // Check all active consumed frames for a suppressedUntil that covers this token.
  // Per RFC 0002, we examine all frames on the stack and take the maximum
  // suppressedUntil.  Only frames that have actually been consumed (i.e.,
  // their finish() was called with a position beyond the anchor) participate
  // in suppression.
  for (size_t i = 0; i < recoveryFrames.size(); ++i) {
    const RecoveryFrame& frame = recoveryFrames[i];
    if (frame.consumed && tokenIndex < frame.suppressedUntil) { return true; }
  }
  return false;
}

bool Parser::Impl::rangeIsWrapped(size_t start, size_t end, ast::SyntaxKind open,
                                  ast::SyntaxKind close) const {
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

// RFC 0002: Boundary detection only — forward-scans to find the closing delimiter of a
// balanced group (paren, bracket, or brace) after the caller has already committed to the
// opening delimiter. Advances the cursor past the closing delimiter.
size_t Parser::Impl::consumeBalancedGroupEnd(TokenCursor& cursor, size_t limit,
                                             ast::SyntaxKind open, ast::SyntaxKind close) const {
  if (cursor.position() >= limit || cursor.peek() != open) { return limit; }

  int32_t depth = 0;
  while (cursor.position() < limit) {
    const ast::SyntaxKind kind = cursor.peek();
    if (kind == ast::SyntaxKind::EndOfFile) { return limit; }

    if (kind == open) {
      ++depth;
    } else if (kind == close) {
      --depth;
      if (depth == 0) {
        const size_t closeIndex = cursor.position();
        cursor.advance();
        return closeIndex;
      }
    }

    cursor.advance();
  }

  return limit;
}

// RFC 0002: Boundary detection only — forward-scans for a needle token at nesting depth 0.
// Used by callers that have already committed to a production and need to find a specific
// boundary token (e.g. '=' in an extern type alias, '=>' in a match arm pattern).
size_t Parser::Impl::consumeBalancedUntil(TokenCursor& cursor, size_t limit,
                                          ast::SyntaxKind needle) const {
  int32_t parenDepth = 0;
  int32_t bracketDepth = 0;
  int32_t braceDepth = 0;
  while (cursor.position() < limit) {
    const ast::SyntaxKind kind = cursor.peek();
    if (kind == ast::SyntaxKind::EndOfFile) { return limit; }
    if (kind == needle && parenDepth == 0 && bracketDepth == 0 && braceDepth == 0) {
      return cursor.position();
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
  return limit;
}

// RFC 0002: Boundary detection only — like consumeBalancedUntil but also tracks angle bracket
// depth (with >> splitting enabled) for type contexts. Used only after the caller has committed
// to a type production.
size_t Parser::Impl::consumeBalancedTypeUntil(TokenCursor& cursor, size_t limit,
                                              ast::SyntaxKind needle) const {
  TokenCursor::ScopedSplitMode splitMode(cursor);
  int32_t parenDepth = 0;
  int32_t bracketDepth = 0;
  int32_t braceDepth = 0;
  int32_t angleDepth = 0;
  while (cursor.position() < limit) {
    const ast::SyntaxKind kind = cursor.peek();
    if (kind == ast::SyntaxKind::EndOfFile) { return limit; }
    if (kind == needle && parenDepth == 0 && bracketDepth == 0 && braceDepth == 0 &&
        angleDepth == 0) {
      return cursor.position();
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
  return limit;
}

// RFC 0002: Boundary detection only — forward-scans for an identifier with specific text at
// nesting depth 0. Used for recovery and boundary finding in contexts where the production is
// already identified.
size_t Parser::Impl::consumeBalancedIdentifierUntil(TokenCursor& cursor, size_t limit,
                                                    zc::StringPtr text) const {
  int32_t parenDepth = 0;
  int32_t bracketDepth = 0;
  int32_t braceDepth = 0;
  while (cursor.position() < limit) {
    const ast::SyntaxKind kind = cursor.peek();
    if (kind == ast::SyntaxKind::EndOfFile) { return limit; }
    if (kind == ast::SyntaxKind::Identifier && parenDepth == 0 && bracketDepth == 0 &&
        braceDepth == 0 && cursor.token().getValue() == text) {
      return cursor.position();
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
  return limit;
}

size_t Parser::Impl::consumeBalancedTypeIdentifierUntil(TokenCursor& cursor, size_t limit,
                                                        zc::StringPtr text) const {
  TokenCursor::ScopedSplitMode splitMode(cursor);
  int32_t parenDepth = 0;
  int32_t bracketDepth = 0;
  int32_t braceDepth = 0;
  int32_t angleDepth = 0;
  while (cursor.position() < limit) {
    const ast::SyntaxKind kind = cursor.peek();
    if (kind == ast::SyntaxKind::EndOfFile) { return limit; }
    if (kind == ast::SyntaxKind::Identifier && parenDepth == 0 && bracketDepth == 0 &&
        braceDepth == 0 && angleDepth == 0 && cursor.token().getValue() == text) {
      return cursor.position();
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
  return limit;
}

bool Parser::Impl::isIdentifierText(size_t index, zc::StringPtr text) const {
  return kindAt(index) == ast::SyntaxKind::Identifier && tokenAt(index).getValue() == text;
}

size_t Parser::Impl::modulePathSeparatorWidth(size_t index, size_t end) const {
  if (index < end && kindAt(index) == ast::SyntaxKind::Period) { return 1; }
  if (index < end && kindAt(index) == ast::SyntaxKind::ColonColon) { return 1; }
  return 0;
}

bool Parser::Impl::modulePathSeparatorPrecedesGroup(size_t index, size_t end) const {
  const size_t width = modulePathSeparatorWidth(index, end);
  return width > 0 && index + width < end && kindAt(index + width) == ast::SyntaxKind::LeftBrace;
}

uint32_t Parser::Impl::attributePathSegmentCount(size_t start, size_t end) const {
  uint32_t count = 0;
  for (size_t index = start; index < end; ++index) {
    if (isAttributePathSegment(kindAt(index))) { ++count; }
  }
  return count;
}

bool Parser::Impl::modifierGroupContains(size_t start, size_t end, ast::SyntaxKind needle,
                                         size_t& found) const {
  for (size_t cursor = start; cursor < end; ++cursor) {
    if (kindAt(cursor) == ast::SyntaxKind::EndOfFile) { return false; }
    if (kindAt(cursor) == needle) {
      found = cursor;
      return true;
    }
  }
  return false;
}

size_t Parser::Impl::consumeBracedBodyEnd(size_t bodyOpen, size_t limit) const {
  RecoveryFrameScope recoveryFrame(*this, RecoveryContext::Statement, bodyOpen);
  if (bodyOpen >= limit || kindAt(bodyOpen) != ast::SyntaxKind::LeftBrace) {
    return recoveryFrame.finish(consumeSimpleStatementEnd(bodyOpen, limit));
  }

  const size_t closeBrace = findMatchingRightBrace(bodyOpen, limit);
  if (closeBrace < limit) { return recoveryFrame.finish(closeBrace + 1); }
  const bool unboundedLimit = limit == static_cast<size_t>(-1);
  size_t recoveryEnd = limit;
  if (unboundedLimit) {
    TokenCursor recoveryCursor = tokenCursorAt(bodyOpen);
    while (!recoveryCursor.isAtEnd()) { recoveryCursor.advance(); }
    recoveryEnd = recoveryCursor.position();
  }
  const size_t diagnosticIndex = unboundedLimit || recoveryEnd == 0 ? recoveryEnd : recoveryEnd - 1;
  if (!shouldSuppressDiagnostic(diagnosticIndex)) {
    diagnosticEngine.diagnose<diagnostics::DiagID::ExpectedToken>(
        tokenAt(diagnosticIndex).getLocation(), "}"_zc);
  }
  return recoveryFrame.finish(recoveryEnd);
}

size_t Parser::Impl::findFunctionBodyOpenAfterParams(size_t closeParen, size_t limit) const {
  size_t cursor = closeParen + 1;
  while (cursor < limit) {
    if (kindAt(cursor) == ast::SyntaxKind::EndOfFile) { return limit; }
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

Parser::Impl::SourceElementBoundary Parser::Impl::consumeSourceElement(TokenCursor& cursor,
                                                                       size_t limit) const {
  const size_t start = cursor.position();
  RecoveryFrameScope recoveryFrame(*this, RecoveryContext::SourceFile, start);
  SourceElementBoundary boundary;
  boundary.start = start;
  boundary.nodeStart = start;
  boundary.head = start;
  boundary.end = limit;
  if (start >= limit) {
    cursor.moveTo(limit);
    recoveryFrame.finish(limit);
    return boundary;
  }

  const size_t nodeStart = skipOuterAttributePrefix(start, limit);
  boundary.nodeStart = nodeStart;
  cursor.moveTo(nodeStart);

  // Detect dangling '#' that is not followed by '[' for an attribute.
  // Emit the specific diagnostic and skip the '#' so it is not also
  // reported as a generic "Expression expected".
  if (kindAt(start) == ast::SyntaxKind::Hash && start == nodeStart) {
    if (!shouldSuppressDiagnostic(start)) {
      diagnosticEngine.diagnose<diagnostics::DiagID::DanglingHash>(diagnosticLoc(start))
          .addChild(zc::heap<diagnostics::Diagnostic>(diagnostics::DiagID::DanglingHashHelp,
                                                      diagnosticLoc(start)));
    }
    const size_t afterHash = start + 1;
    boundary.nodeStart = afterHash;
    cursor.moveTo(afterHash);
    if (afterHash >= limit) {
      boundary.head = afterHash;
      boundary.end = limit;
      recoveryFrame.finish(limit);
      return boundary;
    }
  }

  if (nodeStart >= limit) {
    boundary.head = nodeStart;
    boundary.end = limit;
    cursor.moveTo(limit);
    recoveryFrame.finish(limit);
    return boundary;
  }

  while (cursor.position() < limit && cursor.peek() != ast::SyntaxKind::EndOfFile &&
         isNamedDeclarationModifier(cursor.peek())) {
    cursor.advance();
  }
  const size_t head = cursor.position();
  boundary.head = head;
  if (head >= limit) {
    boundary.end = limit;
    cursor.moveTo(limit);
    recoveryFrame.finish(limit);
    return boundary;
  }

  switch (cursor.peek()) {
    case ast::SyntaxKind::ModuleKeyword:
      boundary.kind = ast::SyntaxKind::ModuleDeclaration;
      boundary.end = consumeBracedDeclarationEnd(head, limit);
      break;
    case ast::SyntaxKind::ImportKeyword:
      boundary.kind = ast::SyntaxKind::ImportDeclaration;
      boundary.end = consumeSimpleStatementEnd(head, limit);
      break;
    case ast::SyntaxKind::ExportKeyword:
      boundary.kind = head + 1 < limit && kindAt(head + 1) == ast::SyntaxKind::ModuleKeyword
                          ? ast::SyntaxKind::ModuleDeclaration
                          : ast::SyntaxKind::ExportDeclaration;
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
      // In statement position, '{' always starts a block statement.
      // Object literals as statements must be parenthesized: ({...}).
      // This removes the looksLikeObjectLiteralExpression range-scanning
      // heuristic that chose between BlockStmt and ExpressionStatement by
      // scanning the token interior (RFC 0002, AC-09).
      boundary.kind = ast::SyntaxKind::BlockStmt;
      break;
    case ast::SyntaxKind::Identifier:
      if (isExternDeclarationStart(head, limit)) {
        boundary.kind = ast::SyntaxKind::ExternBlock;
        boundary.end = consumeExternDeclarationEnd(head, limit);
      } else if (isSoftKeyword(head, "impl"_zc) ||
                 (isSoftKeyword(head, "unsafe"_zc) && head + 1 < limit &&
                  isSoftKeyword(head + 1, "impl"_zc))) {
        boundary.kind = isMarkerImplDeclarationStart(head, limit)
                            ? ast::SyntaxKind::MarkerImpl
                            : ast::SyntaxKind::StandaloneImplDecl;
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
  recoveryFrame.finish(boundary.end);
  return boundary;
}

Parser::Impl::SourceElementParseResult Parser::Impl::parseSourceElement(
    AstFactory& builder, TokenCursor& cursor, size_t limit,
    SourceElementContext elementContext) const {
  SourceElementParseResult result;
  result.boundary = consumeSourceElement(cursor, limit);
  if (result.boundary.start >= limit || result.boundary.nodeStart >= limit ||
      result.boundary.head >= limit || result.boundary.end <= result.boundary.start) {
    result.boundary.end = limit;
    return result;
  }

  result.attrs = parseOuterAttributeList(builder, result.boundary.start, result.boundary.end);
  if (result.attrs && result.boundary.kind == ast::SyntaxKind::ModuleDeclaration) {
    diagnosticEngine.diagnose<diagnostics::DiagID::AttributeRequiresSupportedTarget>(
        tokenAt(result.boundary.start).getLocation());
  }
  for (size_t index = result.boundary.nodeStart; index < result.boundary.head; ++index) {
    const ast::SyntaxKind modifier = kindAt(index);
    if (modifier == ast::SyntaxKind::PublicKeyword || modifier == ast::SyntaxKind::PrivateKeyword ||
        modifier == ast::SyntaxKind::ProtectedKeyword) {
      diagnosticEngine.diagnose<diagnostics::DiagID::VisibilityModifierRequiresMemberContext>(
          tokenAt(index).getLocation(), tokenLabel(tokenAt(index)));
      break;
    }
  }
  if (result.boundary.start < result.boundary.nodeStart &&
      result.boundary.kind == ast::SyntaxKind::ExpressionStatement) {
    diagnosticEngine.diagnose<diagnostics::DiagID::UnexpectedTokenExpected>(
        tokenAt(result.boundary.start).getLocation());
    return result;
  }
  result.node = parseSourceElementOfKind(builder, result.boundary.nodeStart, result.boundary.end,
                                         result.boundary.kind, elementContext);
  return result;
}

void Parser::Impl::conditionRangeAfterKeyword(size_t start, size_t end, size_t& condStart,
                                              size_t& condEnd, size_t& bodyStart) const {
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

  TokenCursor braceCursor = tokenCursorAt(condStart);
  const size_t brace = consumeBalancedUntil(braceCursor, end, ast::SyntaxKind::LeftBrace);
  condEnd = brace < end ? brace : end;
  bodyStart = brace < end ? brace : end;
}

ast::NodeId Parser::Impl::parseSourceElementOfKind(AstFactory& builder, size_t start, size_t end,
                                                   ast::SyntaxKind kind,
                                                   SourceElementContext elementContext) const {
  if (start >= end) { return ast::NodeId(); }

  const bool moduleItemOnly =
      kind == ast::SyntaxKind::ImportDeclaration || kind == ast::SyntaxKind::ExportDeclaration;
  if (moduleItemOnly && elementContext != SourceElementContext::ModuleItem) {
    diagnosticEngine.diagnose<diagnostics::DiagID::ImportOrExportDeclarationRequiresModuleScope>(
        diagnosticLoc(start));
    return ast::NodeId();
  }

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
    case ast::SyntaxKind::ExternBlock:
      return parseExternBlockDeclaration(builder, start, end);
    case ast::SyntaxKind::StandaloneImplDecl:
      return parseStandaloneImplDeclaration(builder, start, end);
    case ast::SyntaxKind::MarkerImpl:
      return parseMarkerImplDeclaration(builder, start, end);
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
      return builder.makeEmptyStatement(rangeFor(start, end));
    case ast::SyntaxKind::DebuggerStatement:
      if (!requireTrailingSemicolon(start, end)) { return ast::NodeId(); }
      return builder.makeDebuggerStatement(rangeFor(start, end));
    case ast::SyntaxKind::ExpressionStatement:
      if (kindAt(start) == ast::SyntaxKind::SpawnKeyword) {
        return parseSpawnStatement(builder, start, end);
      }
      return parseExpressionStatement(builder, start, end);
    default:
      return parseExpressionStatement(builder, start, end);
  }
}

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
