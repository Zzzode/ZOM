// Copyright (c) 2024-2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#include "zomlang/compiler/lexer/lexer.h"

#include <cctype>

#include "zc/core/common.h"
#include "zc/core/string.h"
#include "zomlang/compiler/basic/frontend.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/diagnostics/in-flight-diagnostic.h"
#include "zomlang/compiler/lexer/token.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang {
namespace compiler {
namespace lexer {

struct Lexer::Impl {
  /// Reference members
  const source::SourceManager& sourceMgr;
  diagnostics::DiagnosticEngine& diagnosticEngine;
  /// Language options
  const basic::LangOptions& langOpts;
  /// Buffer ID for the buffer being lexed
  const source::BufferId& bufferId;
  /// Buffer states
  const zc::byte* bufferStart;
  const zc::byte* bufferEnd;
  const zc::byte* curPtr;

  // Token state
  Token nextToken;
  LexerMode currentMode;
  CommentRetentionMode commentMode;

  Impl(const source::SourceManager& sourceMgr, diagnostics::DiagnosticEngine& diagnosticEngine,
       const basic::LangOptions& options, const source::BufferId& bufferId)
      : sourceMgr(sourceMgr),
        diagnosticEngine(diagnosticEngine),
        langOpts(options),
        bufferId(bufferId),
        currentMode(LexerMode::kNormal),
        commentMode(CommentRetentionMode::kNone) {
    // Initialize buffer pointers
    zc::ArrayPtr<const zc::byte> buffer = sourceMgr.getEntireTextForBuffer(bufferId);
    bufferStart = buffer.begin();
    bufferEnd = buffer.end();
    curPtr = bufferStart;
  }

  /// Utility functions
  const zc::byte* getBufferPtrForSourceLoc(source::SourceLoc loc) const;

  void formToken(TokenKind kind, const zc::byte* tokStart) {
    source::SourceLoc startLoc = sourceMgr.getLocForOffset(bufferId, tokStart - bufferStart);
    source::SourceLoc endLoc = sourceMgr.getLocForOffset(bufferId, curPtr - bufferStart);
    nextToken = Token(kind, source::SourceRange(startLoc, endLoc));
  }

  /// Lexing implementation
  void lexImpl() {
    skipTrivia();
    if (curPtr >= bufferEnd) {
      formToken(TokenKind::kEOF, curPtr);
      return;
    }
    scanToken();
  }

  /// Token scanning
  void scanToken() {
    const zc::byte* tokStart = curPtr;
    zc::byte c = *curPtr++;

    switch (c) {
      case '(':
        formToken(TokenKind::kLeftParen, tokStart);
        break;
      case ')':
        formToken(TokenKind::kRightParen, tokStart);
        break;
      case '{':
        formToken(TokenKind::kLeftBrace, tokStart);
        break;
      case '}':
        formToken(TokenKind::kRightBrace, tokStart);
        break;
      case ',':
        formToken(TokenKind::kComma, tokStart);
        break;
      case ':':
        formToken(TokenKind::kColon, tokStart);
        break;
      case '-':
        if (curPtr < bufferEnd && *curPtr == '>') {
          curPtr++;
          formToken(TokenKind::kArrow, tokStart);
        } else if (curPtr < bufferEnd && *curPtr == '-') {
          curPtr++;
          formToken(TokenKind::kMinusMinus, tokStart);
        } else if (curPtr < bufferEnd && *curPtr == '=') {
          curPtr++;
          formToken(TokenKind::kMinusEquals, tokStart);
        } else {
          formToken(TokenKind::kMinus, tokStart);
        }
        break;
      case '+':
        if (curPtr < bufferEnd && *curPtr == '+') {
          curPtr++;
          formToken(TokenKind::kPlusPlus, tokStart);
        } else if (curPtr < bufferEnd && *curPtr == '=') {
          curPtr++;
          formToken(TokenKind::kPlusEquals, tokStart);
        } else {
          formToken(TokenKind::kPlus, tokStart);
        }
        break;
      case '*':
        if (curPtr < bufferEnd && *curPtr == '*') {
          curPtr++;
          if (curPtr < bufferEnd && *curPtr == '=') {
            curPtr++;
            formToken(TokenKind::kAsteriskAsteriskEquals, tokStart);
          } else {
            formToken(TokenKind::kAsteriskAsterisk, tokStart);
          }
        } else if (curPtr < bufferEnd && *curPtr == '=') {
          curPtr++;
          formToken(TokenKind::kAsteriskEquals, tokStart);
        } else {
          formToken(TokenKind::kAsterisk, tokStart);
        }
        break;
      case '/':
        if (curPtr < bufferEnd && *curPtr == '=') {
          curPtr++;
          formToken(TokenKind::kSlashEquals, tokStart);
        } else if (curPtr < bufferEnd && *curPtr == '/') {
          lexComment();  // Already handles forming the token or skipping
          return;        // lexComment advances curPtr and forms token if needed
        } else {
          formToken(TokenKind::kSlash, tokStart);
        }
        break;
      case '%':
        if (curPtr < bufferEnd && *curPtr == '=') {
          curPtr++;
          formToken(TokenKind::kPercentEquals, tokStart);
        } else {
          formToken(TokenKind::kPercent, tokStart);
        }
        break;
      case '<':
        if (curPtr < bufferEnd && *curPtr == '=') {
          curPtr++;
          formToken(TokenKind::kLessThanEquals, tokStart);
        } else if (curPtr < bufferEnd && *curPtr == '<') {
          curPtr++;
          if (curPtr < bufferEnd && *curPtr == '=') {
            curPtr++;
            formToken(TokenKind::kLessThanLessThanEquals, tokStart);
          } else {
            formToken(TokenKind::kLessThanLessThan, tokStart);
          }
        } else if (curPtr < bufferEnd && *curPtr == '/') {
          curPtr++;
          formToken(TokenKind::kLessThanSlash, tokStart);
        } else {
          formToken(TokenKind::kLessThan, tokStart);
        }
        break;
      case '>':
        if (curPtr < bufferEnd && *curPtr == '=') {
          curPtr++;
          formToken(TokenKind::kGreaterThanEquals, tokStart);
        } else if (curPtr < bufferEnd && *curPtr == '>') {
          curPtr++;
          if (curPtr < bufferEnd && *curPtr == '>') {
            curPtr++;
            if (curPtr < bufferEnd && *curPtr == '=') {
              curPtr++;
              formToken(TokenKind::kGreaterThanGreaterThanGreaterThanEquals, tokStart);
            } else {
              formToken(TokenKind::kGreaterThanGreaterThanGreaterThan, tokStart);
            }
          } else if (curPtr < bufferEnd && *curPtr == '=') {
            curPtr++;
            formToken(TokenKind::kGreaterThanGreaterThanEquals, tokStart);
          } else {
            formToken(TokenKind::kGreaterThanGreaterThan, tokStart);
          }
        } else {
          formToken(TokenKind::kGreaterThan, tokStart);
        }
        break;
      case '=':
        if (curPtr < bufferEnd && *curPtr == '=') {
          curPtr++;
          if (curPtr < bufferEnd && *curPtr == '=') {
            curPtr++;
            formToken(TokenKind::kEqualsEqualsEquals, tokStart);
          } else {
            formToken(TokenKind::kEqualsEquals, tokStart);
          }
        } else if (curPtr < bufferEnd && *curPtr == '>') {
          curPtr++;
          formToken(TokenKind::kEqualsGreaterThan, tokStart);
        } else {
          formToken(TokenKind::kEquals, tokStart);
        }
        break;
      case '!':
        if (curPtr < bufferEnd && *curPtr == '=') {
          curPtr++;
          if (curPtr < bufferEnd && *curPtr == '=') {
            curPtr++;
            formToken(TokenKind::kExclamationEqualsEquals, tokStart);
          } else {
            formToken(TokenKind::kExclamationEquals, tokStart);
          }
        } else {
          formToken(TokenKind::kExclamation, tokStart);
        }
        break;
      case '&':
        if (curPtr < bufferEnd && *curPtr == '&') {
          curPtr++;
          if (curPtr < bufferEnd && *curPtr == '=') {
            curPtr++;
            formToken(TokenKind::kAmpersandAmpersandEquals, tokStart);
          } else {
            formToken(TokenKind::kAmpersandAmpersand, tokStart);
          }
        } else if (curPtr < bufferEnd && *curPtr == '=') {
          curPtr++;
          formToken(TokenKind::kAmpersandEquals, tokStart);
        } else {
          formToken(TokenKind::kAmpersand, tokStart);
        }
        break;
      case '|':
        if (curPtr < bufferEnd && *curPtr == '|') {
          curPtr++;
          if (curPtr < bufferEnd && *curPtr == '=') {
            curPtr++;
            formToken(TokenKind::kBarBarEquals, tokStart);
          } else {
            formToken(TokenKind::kBarBar, tokStart);
          }
        } else if (curPtr < bufferEnd && *curPtr == '=') {
          curPtr++;
          formToken(TokenKind::kBarEquals, tokStart);
        } else {
          formToken(TokenKind::kBar, tokStart);
        }
        break;
      case '^':
        if (curPtr < bufferEnd && *curPtr == '=') {
          curPtr++;
          formToken(TokenKind::kCaretEquals, tokStart);
        } else {
          formToken(TokenKind::kCaret, tokStart);
        }
        break;
      case '~':
        formToken(TokenKind::kTilde, tokStart);
        break;
      case '?':
        if (curPtr < bufferEnd && *curPtr == '?') {
          curPtr++;
          if (curPtr < bufferEnd && *curPtr == '=') {
            curPtr++;
            formToken(TokenKind::kQuestionQuestionEquals, tokStart);
          } else {
            formToken(TokenKind::kQuestionQuestion, tokStart);
          }
        } else if (curPtr < bufferEnd && *curPtr == '.') {
          curPtr++;
          formToken(TokenKind::kQuestionDot, tokStart);
        } else {
          formToken(TokenKind::kQuestion, tokStart);
        }
        break;
      case '.':
        if (curPtr < bufferEnd && *curPtr == '.' && curPtr + 1 < bufferEnd &&
            *(curPtr + 1) == '.') {
          curPtr += 2;
          formToken(TokenKind::kDotDotDot, tokStart);
        } else {
          formToken(TokenKind::kPeriod, tokStart);
        }
        break;
      case ';':
        formToken(TokenKind::kSemicolon, tokStart);
        break;
      case '[':
        formToken(TokenKind::kLeftBracket, tokStart);
        break;
      case ']':
        formToken(TokenKind::kRightBracket, tokStart);
        break;
      case '@':
        formToken(TokenKind::kAt, tokStart);
        break;
      case '#':
        formToken(TokenKind::kHash, tokStart);
        break;
      case '`':
        formToken(TokenKind::kBacktick, tokStart);
        break;
      default:
        if (isIdentifierStart(c)) {
          lexIdentifier();
        } else if (isdigit(c)) {
          lexNumber();
        } else if (c == '"') {
          lexStringLiteralImpl();
        } else {
          formToken(TokenKind::kUnknown, tokStart);
        }
        break;
    }
  }

  /// Newline handling
  void handleNewline() {
    curPtr++;  // Skip newline character
  }

  /// Trivia
  void skipTrivia() {
    while (curPtr < bufferEnd) {
      zc::byte c = *curPtr;
      if (c == ' ' || c == '\t' || c == '\r') {
        curPtr++;
      } else if (c == '\n') {
        handleNewline();
      } else if (c == '/' && curPtr + 1 < bufferEnd && *(curPtr + 1) == '/') {
        lexComment();
      } else {
        break;
      }
    }
  }

  void lexIdentifier() {
    // Go back one character, as the first character has already been read
    const zc::byte* tokStart = curPtr - 1;

    while (curPtr < bufferEnd && isIdentifierContinuation(*curPtr)) { curPtr++; }

    // Check if it's a keyword
    zc::String text = zc::str(zc::ArrayPtr<const zc::byte>(tokStart, curPtr));

    TokenKind kind = TokenKind::kIdentifier;
    if (text == "fun") {
      kind = TokenKind::kFunKeyword;
    } else if (text == "i32" || text == "str" || text == "bool" || text == "f64") {
      // These are now just identifiers, type checking will handle them
      kind = TokenKind::kIdentifier;
    }

    formToken(kind, tokStart);
  }

  void lexNumber() {
    // Go back one character, as the first character has already been read
    const zc::byte* tokStart = curPtr - 1;

    // Read integer part
    while (curPtr < bufferEnd && isdigit(*curPtr)) { curPtr++; }

    TokenKind kind = TokenKind::kIntegerLiteral;

    // Check if it's a floating-point number
    if (curPtr < bufferEnd && *curPtr == '.' && curPtr + 1 < bufferEnd && isdigit(*(curPtr + 1))) {
      curPtr++;  // Skip '.'
      while (curPtr < bufferEnd && isdigit(*curPtr)) { curPtr++; }
      kind = TokenKind::kFloatLiteral;
    }

    formToken(kind, tokStart);
  }
  void lexStringLiteralImpl() {
    const zc::byte* tokStart =
        curPtr - 1;  // Go back one character, as the first character has already been read

    // Skip the starting quote, look for the ending quote
    while (curPtr < bufferEnd) {
      zc::byte c = *curPtr++;
      if (c == '"') {
        // Found the ending quote
        break;
      } else if (c == '\\' && curPtr < bufferEnd) {
        // Handle escape character
        curPtr++;  // Skip escape character
      } else if (c == '\n') {
        // String cannot span multiple lines (unless escaped)
        // An error can be reported here, but for now, handle it simply
        break;
      }
    }

    formToken(TokenKind::kStringLiteral, tokStart);
  }
  void lexEscapedIdentifier() { /*...*/ }
  void lexOperator() { /*...*/ }

  /// Unicode handling
  uint32_t lexUnicodeScalarValue() { /*...*/ return 0; }

  /// Comments
  void lexComment() {
    const zc::byte* tokStart = curPtr;

    // Skip '//'
    curPtr += 2;

    // Read to the end of the line
    while (curPtr < bufferEnd && *curPtr != '\n') { curPtr++; }

    if (commentMode == CommentRetentionMode::kReturnAsTokens) {
      formToken(TokenKind::kComment, tokStart);
    }
    // If comments are not retained, just skip them
  }

  /// Preprocessor directives
  void lexPreprocessorDirective() { /*...*/ }

  /// Multibyte character handling
  bool tryLexMultibyteCharacter() { /*...*/ return false; }

  /// Error recovery
  void recoverFromLexingError() { /*...*/ }

  /// Buffer management
  void refillBuffer() { /*...*/ }

  /// State checks
  bool isAtStartOfLine() const { return curPtr == bufferStart || *(curPtr - 1) == '\n'; }
  bool isAtEndOfFile() const { return curPtr >= bufferEnd; }

  /// Helper functions
  bool isIdentifierStart(zc::byte c) const { return isalpha(c) || c == '_'; }
  bool isIdentifierContinuation(zc::byte c) const { return isalnum(c) || c == '_'; }
  bool isOperatorStart(zc::byte c) const {
    return c == '+' || c == '-' || c == '*' || c == '/' || c == '=' || c == '<' || c == '>' ||
           c == '!' || c == '&' || c == '|';
  }
};

Lexer::Lexer(const source::SourceManager& sourceMgr,
             diagnostics::DiagnosticEngine& diagnosticEngine, const basic::LangOptions& options,
             const source::BufferId& bufferId)
    : impl(zc::heap<Impl>(sourceMgr, diagnosticEngine, options, bufferId)) {}
Lexer::~Lexer() = default;

const zc::byte* Lexer::Impl::getBufferPtrForSourceLoc(const source::SourceLoc loc) const {
  return bufferStart + sourceMgr.getLocOffsetInBuffer(loc, bufferId);
}

const zc::byte* Lexer::getBufferPtrForSourceLoc(source::SourceLoc Loc) const {
  return impl->getBufferPtrForSourceLoc(Loc);
}

void Lexer::lex(Token& result) {
  result = impl->nextToken;

  if (impl->isAtEndOfFile()) {
    result.setKind(TokenKind::kEOF);
    return;
  }

  impl->lexImpl();
}

const Token& Lexer::peekNextToken() const { return impl->nextToken; }

LexerState Lexer::getStateForBeginningOfToken(const Token& tok) const {
  return LexerState(tok.getLocation(), impl->currentMode);
}

void Lexer::restoreState(LexerState s, bool enableDiagnostics) {
  impl->curPtr = getBufferPtrForSourceLoc(s.Loc);
  impl->currentMode = s.mode;
  impl->lexImpl();

  // Don't re-emit diagnostics from re-advancing the lexer.
  if (enableDiagnostics) {
    // impl->diags.ignoreInFlightDiagnostics();
  }
}

void Lexer::enterMode(LexerMode mode) { impl->currentMode = mode; }

void Lexer::exitMode(LexerMode mode) {
  if (impl->currentMode == mode) { impl->currentMode = LexerMode::kNormal; }
}

// Full implementations of other methods...
unsigned Lexer::lexUnicodeEscape(const zc::byte*& curPtr, diagnostics::DiagnosticEngine& diags) {
  // Original implementation...
  return 0;
}

bool Lexer::tryLexRegexLiteral(const zc::byte* tokStart) {
  // Original implementation...
  return false;
}

void Lexer::lexStringLiteral(unsigned customDelimiterLen) { impl->lexStringLiteralImpl(); }

bool Lexer::isCodeCompletion() const { return impl->curPtr >= impl->bufferEnd; }

void Lexer::setCommentRetentionMode(CommentRetentionMode mode) { impl->commentMode = mode; }

source::SourceLoc Lexer::getLocForStartOfToken(source::SourceLoc loc) const {
  if (loc.isInvalid()) { return {}; }
  return {};
}

source::CharSourceRange Lexer::getCharSourceRangeFromSourceRange(
    const source::SourceRange& sr) const {
  return source::CharSourceRange(sr.getStart(), sr.getEnd());
}

}  // namespace lexer
}  // namespace compiler
}  // namespace zomlang
