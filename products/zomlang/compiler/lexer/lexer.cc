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
          lexSingleLineComment();
          return;
        } else if (curPtr < bufferEnd && *curPtr == '*') {
          lexMultiLineComment();
          return;
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
        } else if (c == '\'') {
          lexSingleQuoteString();
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
        lexSingleLineComment();
      } else if (c == '/' && curPtr + 1 < bufferEnd && *(curPtr + 1) == '*') {
        lexMultiLineComment();
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

    TokenKind kind = getKeywordKind(text);
    if (kind == TokenKind::kUnknown) { kind = TokenKind::kIdentifier; }

    formToken(kind, tokStart);
  }

  TokenKind getKeywordKind(const zc::StringPtr text) {
    // Keywords from ZomLexer.g4
    if (text == "abstract") return TokenKind::kAbstractKeyword;
    if (text == "accessor") return TokenKind::kAccessorKeyword;
    if (text == "any") return TokenKind::kAnyKeyword;
    if (text == "as") return TokenKind::kAsKeyword;
    if (text == "asserts") return TokenKind::kAssertsKeyword;
    if (text == "assert") return TokenKind::kAssertKeyword;
    if (text == "async") return TokenKind::kAsyncKeyword;
    if (text == "await") return TokenKind::kAwaitKeyword;
    if (text == "bigint") return TokenKind::kBigIntKeyword;
    if (text == "boolean") return TokenKind::kBooleanKeyword;
    if (text == "break") return TokenKind::kBreakKeyword;
    if (text == "case") return TokenKind::kCaseKeyword;
    if (text == "catch") return TokenKind::kCatchKeyword;
    if (text == "class") return TokenKind::kClassKeyword;
    if (text == "continue") return TokenKind::kContinueKeyword;
    if (text == "const") return TokenKind::kConstKeyword;
    if (text == "constructor") return TokenKind::kConstructorKeyword;
    if (text == "debugger") return TokenKind::kDebuggerKeyword;
    if (text == "declare") return TokenKind::kDeclareKeyword;
    if (text == "default") return TokenKind::kDefaultKeyword;
    if (text == "delete") return TokenKind::kDeleteKeyword;
    if (text == "do") return TokenKind::kDoKeyword;
    if (text == "extends") return TokenKind::kExtendsKeyword;
    if (text == "export") return TokenKind::kExportKeyword;
    if (text == "false") return TokenKind::kFalseKeyword;
    if (text == "finally") return TokenKind::kFinallyKeyword;
    if (text == "from") return TokenKind::kFromKeyword;
    if (text == "fun") return TokenKind::kFunKeyword;
    if (text == "get") return TokenKind::kGetKeyword;
    if (text == "global") return TokenKind::kGlobalKeyword;
    if (text == "if") return TokenKind::kIfKeyword;
    if (text == "immediate") return TokenKind::kImmediateKeyword;
    if (text == "implements") return TokenKind::kImplementsKeyword;
    if (text == "import") return TokenKind::kImportKeyword;
    if (text == "in") return TokenKind::kInKeyword;
    if (text == "infer") return TokenKind::kInferKeyword;
    if (text == "instanceof") return TokenKind::kInstanceOfKeyword;
    if (text == "interface") return TokenKind::kInterfaceKeyword;
    if (text == "intrinsic") return TokenKind::kIntrinsicKeyword;
    if (text == "is") return TokenKind::kIsKeyword;
    if (text == "keyof") return TokenKind::kKeyOfKeyword;
    if (text == "let") return TokenKind::kLetKeyword;
    if (text == "match") return TokenKind::kMatchKeyword;
    if (text == "module") return TokenKind::kModuleKeyword;
    if (text == "mutable") return TokenKind::kMutableKeyword;
    if (text == "namespace") return TokenKind::kNamespaceKeyword;
    if (text == "never") return TokenKind::kNeverKeyword;
    if (text == "new") return TokenKind::kNewKeyword;
    if (text == "number") return TokenKind::kNumberKeyword;
    if (text == "null") return TokenKind::kNullKeyword;
    if (text == "object") return TokenKind::kObjectKeyword;
    if (text == "of") return TokenKind::kOfKeyword;
    if (text == "optional") return TokenKind::kOptionalKeyword;
    if (text == "out") return TokenKind::kOutKeyword;
    if (text == "override") return TokenKind::kOverrideKeyword;
    if (text == "package") return TokenKind::kPackageKeyword;
    if (text == "private") return TokenKind::kPrivateKeyword;
    if (text == "protected") return TokenKind::kProtectedKeyword;
    if (text == "public") return TokenKind::kPublicKeyword;
    if (text == "readonly") return TokenKind::kReadonlyKeyword;
    if (text == "require") return TokenKind::kRequireKeyword;
    if (text == "return") return TokenKind::kReturnKeyword;
    if (text == "satisfies") return TokenKind::kSatisfiesKeyword;
    if (text == "set") return TokenKind::kSetKeyword;
    if (text == "static") return TokenKind::kStaticKeyword;
    if (text == "super") return TokenKind::kSuperKeyword;
    if (text == "switch") return TokenKind::kSwitchKeyword;
    if (text == "symbol") return TokenKind::kSymbolKeyword;
    if (text == "this") return TokenKind::kThisKeyword;
    if (text == "throw") return TokenKind::kThrowKeyword;
    if (text == "true") return TokenKind::kTrueKeyword;
    if (text == "try") return TokenKind::kTryKeyword;
    if (text == "typeof") return TokenKind::kTypeOfKeyword;
    if (text == "undefined") return TokenKind::kUndefinedKeyword;
    if (text == "unique") return TokenKind::kUniqueKeyword;
    if (text == "using") return TokenKind::kUsingKeyword;
    if (text == "var") return TokenKind::kVarKeyword;
    if (text == "void") return TokenKind::kVoidKeyword;
    if (text == "when") return TokenKind::kWhenKeyword;
    if (text == "with") return TokenKind::kWithKeyword;
    if (text == "yield") return TokenKind::kYieldKeyword;

    return TokenKind::kUnknown;
  }

  void lexNumber() {
    // Go back one character, as the first character has already been read
    const zc::byte* tokStart = curPtr - 1;
    TokenKind kind = TokenKind::kIntegerLiteral;

    // Check for binary, octal, or hex literals
    if (*tokStart == '0' && curPtr < bufferEnd) {
      zc::byte nextChar = *curPtr;
      if (nextChar == 'b' || nextChar == 'B') {
        // Binary literal
        curPtr++;  // Skip 'b' or 'B'
        while (curPtr < bufferEnd && (*curPtr == '0' || *curPtr == '1' || *curPtr == '_')) {
          curPtr++;
        }
        formToken(TokenKind::kIntegerLiteral, tokStart);
        return;
      } else if (nextChar == 'o' || nextChar == 'O') {
        // Octal literal
        curPtr++;  // Skip 'o' or 'O'
        while (curPtr < bufferEnd && ((*curPtr >= '0' && *curPtr <= '7') || *curPtr == '_')) {
          curPtr++;
        }
        formToken(TokenKind::kIntegerLiteral, tokStart);
        return;
      } else if (nextChar == 'x' || nextChar == 'X') {
        // Hexadecimal literal
        curPtr++;  // Skip 'x' or 'X'
        while (curPtr < bufferEnd && (isdigit(*curPtr) || (*curPtr >= 'a' && *curPtr <= 'f') ||
                                      (*curPtr >= 'A' && *curPtr <= 'F') || *curPtr == '_')) {
          curPtr++;
        }
        formToken(TokenKind::kIntegerLiteral, tokStart);
        return;
      }
    }

    // Read decimal integer part (allowing numeric separators)
    while (curPtr < bufferEnd && (isdigit(*curPtr) || *curPtr == '_')) { curPtr++; }

    // Check if it's a floating-point number
    if (curPtr < bufferEnd && *curPtr == '.' && curPtr + 1 < bufferEnd && isdigit(*(curPtr + 1))) {
      curPtr++;  // Skip '.'
      while (curPtr < bufferEnd && (isdigit(*curPtr) || *curPtr == '_')) { curPtr++; }
      kind = TokenKind::kFloatLiteral;
    }

    // Check for exponent part
    if (curPtr < bufferEnd && (*curPtr == 'e' || *curPtr == 'E')) {
      curPtr++;  // Skip 'e' or 'E'
      if (curPtr < bufferEnd && (*curPtr == '+' || *curPtr == '-')) {
        curPtr++;  // Skip sign
      }
      while (curPtr < bufferEnd && (isdigit(*curPtr) || *curPtr == '_')) { curPtr++; }
      kind = TokenKind::kFloatLiteral;
    }

    formToken(kind, tokStart);
  }
  void lexStringLiteralImpl() {
    // Go back one character, as the first character has already been read
    const zc::byte* tokStart = curPtr - 1;
    // Remember which quote character we're using
    zc::byte quoteChar = *(tokStart);

    // Skip the starting quote, look for the ending quote
    while (curPtr < bufferEnd) {
      zc::byte c = *curPtr++;
      if (c == quoteChar) {
        // Found the ending quote
        break;
      } else if (c == '\\' && curPtr < bufferEnd) {
        // Handle escape character
        curPtr++;  // Skip escape character
      } else if (c == '\n' || c == '\r') {
        // String cannot span multiple lines (unless escaped)
        // An error can be reported here, but for now, handle it simply
        break;
      }
    }

    formToken(TokenKind::kStringLiteral, tokStart);
  }

  void lexSingleQuoteString() {
    const zc::byte* tokStart = curPtr - 1;

    // Skip the starting quote, look for the ending quote
    while (curPtr < bufferEnd) {
      zc::byte c = *curPtr++;
      if (c == '\'') {
        // Found the ending quote
        break;
      } else if (c == '\\' && curPtr < bufferEnd) {
        // Handle escape character
        curPtr++;  // Skip escape character
      } else if (c == '\n' || c == '\r') {
        // String cannot span multiple lines (unless escaped)
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
  void lexSingleLineComment() {
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

  void lexMultiLineComment() {
    const zc::byte* tokStart = curPtr;

    // Skip '/*'
    curPtr += 2;

    // Read until we find '*/'
    while (curPtr + 1 < bufferEnd) {
      if (*curPtr == '*' && *(curPtr + 1) == '/') {
        curPtr += 2;  // Skip '*/'
        break;
      }
      curPtr++;
    }

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
