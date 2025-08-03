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
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/in-flight-diagnostic.h"
#include "zomlang/compiler/lexer/token.h"
#include "zomlang/compiler/source/location.h"
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

  // Full start position tracking
  const zc::byte* triviaStartPtr;

  // Token state
  Token nextToken;
  LexerMode currentMode;
  CommentRetentionMode commentMode;

  // Lookahead token cache
  mutable zc::Vector<Token> tokenCache;
  mutable bool cacheInitialized = false;

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
    triviaStartPtr = bufferStart;
  }

  /// Utility functions
  const zc::byte* getBufferPtrForSourceLoc(source::SourceLoc loc) const;

  void formToken(TokenKind kind, const zc::byte* tokStart) {
    source::SourceLoc startLoc = sourceMgr.getLocForOffset(bufferId, tokStart - bufferStart);
    source::SourceLoc endLoc = sourceMgr.getLocForOffset(bufferId, curPtr - bufferStart);
    zc::Maybe<zc::String> cachedText = Token::getStaticTextForTokenKind(kind);
    // For keywords and common operators, cache the text to avoid repeated extraction
    nextToken = Token(kind, source::SourceRange(startLoc, endLoc), zc::mv(cachedText));
  }

  /// Lexing implementation
  void lexImpl() {
    triviaStartPtr = curPtr;
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
        } else if (curPtr < bufferEnd && *curPtr == '!') {
          curPtr++;
          formToken(TokenKind::kErrorUnwrap, tokStart);
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
        } else if (curPtr < bufferEnd && *curPtr == '!') {
          curPtr++;
          formToken(TokenKind::kErrorPropagate, tokStart);
        } else if (curPtr < bufferEnd && *curPtr == ':') {
          curPtr++;
          formToken(TokenKind::kErrorDefault, tokStart);
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
          // Report invalid character error and attempt recovery
          reportInvalidCharacter(c, tokStart);
          recoverFromInvalidCharacter();
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
    auto textPtr = zc::ArrayPtr<const zc::byte>(tokStart, curPtr);

    TokenKind kind = getKeywordKind(textPtr);
    if (kind == TokenKind::kUnknown) {
      kind = TokenKind::kIdentifier;
      // For identifiers, we might want to cache the text since they're frequently accessed
      source::SourceLoc startLoc = sourceMgr.getLocForOffset(bufferId, tokStart - bufferStart);
      source::SourceLoc endLoc = sourceMgr.getLocForOffset(bufferId, curPtr - bufferStart);
      zc::String identifierText = zc::str(textPtr.asChars());
      nextToken = Token(kind, source::SourceRange(startLoc, endLoc), zc::mv(identifierText));
    } else {
      formToken(kind, tokStart);
    }
  }

  TokenKind getKeywordKind(zc::ArrayPtr<const zc::byte> text) {
    // Keywords from ZomLexer.g4
    if (text == "abstract"_zcb) return TokenKind::kAbstractKeyword;
    if (text == "accessor"_zcb) return TokenKind::kAccessorKeyword;
    if (text == "any"_zcb) return TokenKind::kAnyKeyword;
    if (text == "as"_zcb) return TokenKind::kAsKeyword;
    if (text == "asserts"_zcb) return TokenKind::kAssertsKeyword;
    if (text == "assert"_zcb) return TokenKind::kAssertKeyword;
    if (text == "async"_zcb) return TokenKind::kAsyncKeyword;
    if (text == "await"_zcb) return TokenKind::kAwaitKeyword;
    if (text == "bigint"_zcb) return TokenKind::kBigIntKeyword;
    if (text == "boolean"_zcb) return TokenKind::kBooleanKeyword;
    if (text == "break"_zcb) return TokenKind::kBreakKeyword;
    if (text == "case"_zcb) return TokenKind::kCaseKeyword;
    if (text == "catch"_zcb) return TokenKind::kCatchKeyword;
    if (text == "class"_zcb) return TokenKind::kClassKeyword;
    if (text == "continue"_zcb) return TokenKind::kContinueKeyword;
    if (text == "const"_zcb) return TokenKind::kConstKeyword;
    if (text == "constructor"_zcb) return TokenKind::kConstructorKeyword;
    if (text == "debugger"_zcb) return TokenKind::kDebuggerKeyword;
    if (text == "declare"_zcb) return TokenKind::kDeclareKeyword;
    if (text == "default"_zcb) return TokenKind::kDefaultKeyword;
    if (text == "delete"_zcb) return TokenKind::kDeleteKeyword;
    if (text == "do"_zcb) return TokenKind::kDoKeyword;
    if (text == "extends"_zcb) return TokenKind::kExtendsKeyword;
    if (text == "export"_zcb) return TokenKind::kExportKeyword;
    if (text == "false"_zcb) return TokenKind::kFalseKeyword;
    if (text == "finally"_zcb) return TokenKind::kFinallyKeyword;
    if (text == "from"_zcb) return TokenKind::kFromKeyword;
    if (text == "fun"_zcb) return TokenKind::kFunKeyword;
    if (text == "get"_zcb) return TokenKind::kGetKeyword;
    if (text == "global"_zcb) return TokenKind::kGlobalKeyword;
    if (text == "if"_zcb) return TokenKind::kIfKeyword;
    if (text == "immediate"_zcb) return TokenKind::kImmediateKeyword;
    if (text == "implements"_zcb) return TokenKind::kImplementsKeyword;
    if (text == "import"_zcb) return TokenKind::kImportKeyword;
    if (text == "in"_zcb) return TokenKind::kInKeyword;
    if (text == "infer"_zcb) return TokenKind::kInferKeyword;
    if (text == "instanceof"_zcb) return TokenKind::kInstanceOfKeyword;
    if (text == "interface"_zcb) return TokenKind::kInterfaceKeyword;
    if (text == "intrinsic"_zcb) return TokenKind::kIntrinsicKeyword;
    if (text == "is"_zcb) return TokenKind::kIsKeyword;
    if (text == "keyof"_zcb) return TokenKind::kKeyOfKeyword;
    if (text == "let"_zcb) return TokenKind::kLetKeyword;
    if (text == "match"_zcb) return TokenKind::kMatchKeyword;
    if (text == "module"_zcb) return TokenKind::kModuleKeyword;
    if (text == "mutable"_zcb) return TokenKind::kMutableKeyword;
    if (text == "namespace"_zcb) return TokenKind::kNamespaceKeyword;
    if (text == "never"_zcb) return TokenKind::kNeverKeyword;
    if (text == "new"_zcb) return TokenKind::kNewKeyword;
    if (text == "number"_zcb) return TokenKind::kNumberKeyword;
    if (text == "null"_zcb) return TokenKind::kNullKeyword;
    if (text == "object"_zcb) return TokenKind::kObjectKeyword;
    if (text == "of"_zcb) return TokenKind::kOfKeyword;
    if (text == "optional"_zcb) return TokenKind::kOptionalKeyword;
    if (text == "out"_zcb) return TokenKind::kOutKeyword;
    if (text == "override"_zcb) return TokenKind::kOverrideKeyword;
    if (text == "package"_zcb) return TokenKind::kPackageKeyword;
    if (text == "private"_zcb) return TokenKind::kPrivateKeyword;
    if (text == "protected"_zcb) return TokenKind::kProtectedKeyword;
    if (text == "public"_zcb) return TokenKind::kPublicKeyword;
    if (text == "readonly"_zcb) return TokenKind::kReadonlyKeyword;
    if (text == "require"_zcb) return TokenKind::kRequireKeyword;
    if (text == "return"_zcb) return TokenKind::kReturnKeyword;
    if (text == "satisfies"_zcb) return TokenKind::kSatisfiesKeyword;
    if (text == "set"_zcb) return TokenKind::kSetKeyword;
    if (text == "static"_zcb) return TokenKind::kStaticKeyword;
    if (text == "super"_zcb) return TokenKind::kSuperKeyword;
    if (text == "switch"_zcb) return TokenKind::kSwitchKeyword;
    if (text == "symbol"_zcb) return TokenKind::kSymbolKeyword;
    if (text == "this"_zcb) return TokenKind::kThisKeyword;
    if (text == "throw"_zcb) return TokenKind::kThrowKeyword;
    if (text == "true"_zcb) return TokenKind::kTrueKeyword;
    if (text == "try"_zcb) return TokenKind::kTryKeyword;
    if (text == "typeof"_zcb) return TokenKind::kTypeOfKeyword;
    if (text == "undefined"_zcb) return TokenKind::kUndefinedKeyword;
    if (text == "unique"_zcb) return TokenKind::kUniqueKeyword;
    if (text == "using"_zcb) return TokenKind::kUsingKeyword;
    if (text == "var"_zcb) return TokenKind::kVarKeyword;
    if (text == "void"_zcb) return TokenKind::kVoidKeyword;
    if (text == "when"_zcb) return TokenKind::kWhenKeyword;
    if (text == "with"_zcb) return TokenKind::kWithKeyword;
    if (text == "yield"_zcb) return TokenKind::kYieldKeyword;

    // Type keywords
    if (text == "bool"_zcb) return TokenKind::kBoolKeyword;
    if (text == "i8"_zcb) return TokenKind::kI8Keyword;
    if (text == "i32"_zcb) return TokenKind::kI32Keyword;
    if (text == "i64"_zcb) return TokenKind::kI64Keyword;
    if (text == "u8"_zcb) return TokenKind::kU8Keyword;
    if (text == "u16"_zcb) return TokenKind::kU16Keyword;
    if (text == "u32"_zcb) return TokenKind::kU32Keyword;
    if (text == "u64"_zcb) return TokenKind::kU64Keyword;
    if (text == "f32"_zcb) return TokenKind::kF32Keyword;
    if (text == "f64"_zcb) return TokenKind::kF64Keyword;
    if (text == "str"_zcb) return TokenKind::kStrKeyword;
    if (text == "unit"_zcb) return TokenKind::kUnitKeyword;
    if (text == "nil"_zcb) return TokenKind::kNilKeyword;
    if (text == "else"_zcb) return TokenKind::kElseKeyword;
    if (text == "for"_zcb) return TokenKind::kForKeyword;
    if (text == "while"_zcb) return TokenKind::kWhileKeyword;
    if (text == "struct"_zcb) return TokenKind::kStructKeyword;
    if (text == "enum"_zcb) return TokenKind::kEnumKeyword;
    if (text == "error"_zcb) return TokenKind::kErrorKeyword;
    if (text == "alias"_zcb) return TokenKind::kAliasKeyword;
    if (text == "init"_zcb) return TokenKind::kInitKeyword;
    if (text == "deinit"_zcb) return TokenKind::kDeinitKeyword;
    if (text == "raises"_zcb) return TokenKind::kRaisesKeyword;
    if (text == "type"_zcb) return TokenKind::kTypeKeyword;

    return TokenKind::kUnknown;
  }

  void lexNumber() {
    // Go back one character, as the first character has already been read
    const zc::byte* tokStart = curPtr - 1;
    TokenKind kind = TokenKind::kIntegerLiteral;
    bool hasValidDigits = false;

    // Check for binary, octal, or hex literals
    if (*tokStart == '0' && curPtr < bufferEnd) {
      zc::byte nextChar = *curPtr;
      if (nextChar == 'b' || nextChar == 'B') {
        // Binary literal
        curPtr++;  // Skip 'b' or 'B'
        while (curPtr < bufferEnd && (*curPtr == '0' || *curPtr == '1' || *curPtr == '_')) {
          if (*curPtr != '_') hasValidDigits = true;
          curPtr++;
        }
        if (!hasValidDigits) { reportInvalidNumberLiteral("binary"_zc, tokStart); }
        formToken(TokenKind::kIntegerLiteral, tokStart);
        return;
      } else if (nextChar == 'o' || nextChar == 'O') {
        // Octal literal
        curPtr++;  // Skip 'o' or 'O'
        while (curPtr < bufferEnd && ((*curPtr >= '0' && *curPtr <= '7') || *curPtr == '_')) {
          if (*curPtr != '_') { hasValidDigits = true; }
          curPtr++;
        }
        if (!hasValidDigits) { reportInvalidNumberLiteral("octal"_zc, tokStart); }
        formToken(TokenKind::kIntegerLiteral, tokStart);
        return;
      } else if (nextChar == 'x' || nextChar == 'X') {
        // Hexadecimal literal
        curPtr++;  // Skip 'x' or 'X'
        while (curPtr < bufferEnd && (isdigit(*curPtr) || (*curPtr >= 'a' && *curPtr <= 'f') ||
                                      (*curPtr >= 'A' && *curPtr <= 'F') || *curPtr == '_')) {
          if (*curPtr != '_') { hasValidDigits = true; }
          curPtr++;
        }
        if (!hasValidDigits) { reportInvalidNumberLiteral("hexadecimal"_zc, tokStart); }
        formToken(TokenKind::kIntegerLiteral, tokStart);
        return;
      }
    }

    // Read decimal integer part (allowing numeric separators)
    while (curPtr < bufferEnd && (isdigit(*curPtr) || *curPtr == '_')) {
      if (isdigit(*curPtr)) { hasValidDigits = true; }
      curPtr++;
    }

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
      const zc::byte* expStart = curPtr;
      while (curPtr < bufferEnd && (isdigit(*curPtr) || *curPtr == '_')) { curPtr++; }
      // Check if exponent has digits
      if (curPtr == expStart) { reportInvalidNumberLiteral("exponent"_zc, tokStart); }
      kind = TokenKind::kFloatLiteral;
    }

    formToken(kind, tokStart);
  }
  void lexStringLiteralImpl() {
    // Go back one character, as the first character has already been read
    const zc::byte* tokStart = curPtr - 1;
    // Remember which quote character we're using
    zc::byte quoteChar = *(tokStart);
    bool foundClosingQuote = false;

    // Skip the starting quote, look for the ending quote
    while (curPtr < bufferEnd) {
      zc::byte c = *curPtr++;
      if (c == quoteChar) {
        // Found the ending quote
        foundClosingQuote = true;
        break;
      } else if (c == '\\' && curPtr < bufferEnd) {
        // Handle escape character
        zc::byte escaped = *curPtr++;
        // Validate common escape sequences
        if (escaped != 'n' && escaped != 't' && escaped != 'r' && escaped != '\\' &&
            escaped != '"' && escaped != '\'' && escaped != '0' && escaped != 'u' &&
            escaped != 'x') {
          reportInvalidEscapeSequence(escaped, curPtr - 2);
        }
      } else if (c == '\n' || c == '\r') {
        // String cannot span multiple lines (unless escaped)
        reportUnterminatedString(tokStart);
        // Recover by treating as unterminated string
        curPtr--;  // Back up to the newline
        break;
      }
    }

    // Check if we reached end of file without closing quote
    if (!foundClosingQuote && curPtr >= bufferEnd) { reportUnterminatedString(tokStart); }

    formToken(TokenKind::kStringLiteral, tokStart);
  }

  void lexSingleQuoteString() {
    const zc::byte* tokStart = curPtr - 1;
    bool foundClosingQuote = false;
    size_t charCount = 0;

    // Skip the starting quote, look for the ending quote
    while (curPtr < bufferEnd) {
      zc::byte c = *curPtr++;
      if (c == '\'') {
        // Found the ending quote
        foundClosingQuote = true;
        break;
      } else if (c == '\\' && curPtr < bufferEnd) {
        // Handle escape character
        zc::byte escaped = *curPtr++;
        // Validate common escape sequences
        if (escaped != 'n' && escaped != 't' && escaped != 'r' && escaped != '\\' &&
            escaped != '"' && escaped != '\'' && escaped != '0' && escaped != 'u' &&
            escaped != 'x') {
          reportInvalidEscapeSequence(escaped, curPtr - 2);
        }
        charCount++;
      } else if (c == '\n' || c == '\r') {
        // Character literal cannot span multiple lines (unless escaped)
        reportUnterminatedString(tokStart);
        // Recover by treating as unterminated character literal
        curPtr--;  // Back up to the newline
        break;
      } else {
        charCount++;
      }
    }

    // Check if we reached end of file without closing quote
    if (!foundClosingQuote && curPtr >= bufferEnd) { reportUnterminatedString(tokStart); }

    // Character literals should contain exactly one character
    if (foundClosingQuote && charCount != 1) { reportInvalidCharacterLiteral(tokStart); }

    formToken(TokenKind::kCharacterLiteral, tokStart);
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

    // Check if we reached end of file without closing comment
    if (curPtr + 1 >= bufferEnd && !(*curPtr == '*' && *(curPtr + 1) == '/')) {
      reportUnterminatedComment(tokStart);
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

  /// Error recovery and diagnostics
  void reportInvalidCharacter(zc::byte invalidChar, const zc::byte* tokStart) {
    source::SourceLoc loc = sourceMgr.getLocForOffset(bufferId, tokStart - bufferStart);
    diagnosticEngine.diagnose<diagnostics::DiagID::InvalidChar>(loc, zc::str(invalidChar));
  }

  void reportUnterminatedString(const zc::byte* tokStart) {
    source::SourceLoc loc = sourceMgr.getLocForOffset(bufferId, tokStart - bufferStart);
    diagnosticEngine.diagnose<diagnostics::DiagID::UnterminatedString>(loc);
  }

  void reportUnterminatedComment(const zc::byte* tokStart) {
    source::SourceLoc loc = sourceMgr.getLocForOffset(bufferId, tokStart - bufferStart);
    // Use InvalidChar diagnostic for unterminated comments since there's no specific one
    diagnosticEngine.diagnose<diagnostics::DiagID::InvalidChar>(loc, "/*"_zc);
  }

  void reportInvalidCharacterLiteral(const zc::byte* tokStart) {
    source::SourceLoc loc = sourceMgr.getLocForOffset(bufferId, tokStart - bufferStart);
    diagnosticEngine.diagnose<diagnostics::DiagID::InvalidChar>(loc, "character literal"_zc);
  }

  void reportInvalidNumberLiteral(zc::StringPtr numberType, const zc::byte* tokStart) {
    source::SourceLoc loc = sourceMgr.getLocForOffset(bufferId, tokStart - bufferStart);
    // For now, just report as invalid character with the number type
    diagnosticEngine.diagnose<diagnostics::DiagID::InvalidChar>(loc, numberType);
  }

  void reportInvalidEscapeSequence(zc::byte escaped, const zc::byte* tokStart) {
    source::SourceLoc loc = sourceMgr.getLocForOffset(bufferId, tokStart - bufferStart);
    diagnosticEngine.diagnose<diagnostics::DiagID::InvalidChar>(loc, zc::str(escaped));
  }

  void recoverFromInvalidCharacter() {
    // Skip the invalid character and continue lexing
    if (curPtr < bufferEnd) { curPtr++; }
    // Form an unknown token to maintain token stream continuity
    formToken(TokenKind::kUnknown, curPtr - 1);
  }

  void recoverFromLexingError() {
    // General error recovery: skip to next whitespace or known delimiter
    while (curPtr < bufferEnd) {
      zc::byte c = *curPtr;
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ';' || c == ',' || c == '{' ||
          c == '}' || c == '(' || c == ')' || c == '[' || c == ']') {
        break;
      }
      curPtr++;
    }
  }

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

  /// Lookahead functionality
  void initializeTokenCache() const {
    if (cacheInitialized) return;

    // Save current lexer state
    const zc::byte* savedCurPtr = curPtr;
    Token savedNextToken = nextToken;
    LexerMode savedMode = currentMode;

    // Create a temporary lexer state for lookahead
    const_cast<Impl*>(this)->curPtr = savedCurPtr;
    const_cast<Impl*>(this)->currentMode = savedMode;

    // Pre-lex some tokens for lookahead
    const unsigned kInitialCacheSize = 16;
    tokenCache.resize(kInitialCacheSize);

    for (unsigned i = 0; i < kInitialCacheSize && !isAtEndOfFile(); ++i) {
      const_cast<Impl*>(this)->lexImpl();
      tokenCache[i] = nextToken;
      if (nextToken.getKind() == TokenKind::kEOF) {
        tokenCache.resize(i + 1);
        break;
      }
    }

    // Restore original lexer state
    const_cast<Impl*>(this)->curPtr = savedCurPtr;
    const_cast<Impl*>(this)->nextToken = savedNextToken;
    const_cast<Impl*>(this)->currentMode = savedMode;

    cacheInitialized = true;
  }

  const Token& lookAheadToken(unsigned n) const {
    if (n == 0) return nextToken;

    initializeTokenCache();

    // Extend cache if needed
    if (n > tokenCache.size()) {
      // Save current state
      const zc::byte* savedCurPtr = curPtr;
      Token savedNextToken = nextToken;
      LexerMode savedMode = currentMode;

      // Position lexer at the end of cached tokens
      if (!tokenCache.empty()) {
        const Token& lastCachedToken = tokenCache.back();
        if (lastCachedToken.getKind() == TokenKind::kEOF) { return lastCachedToken; }
        // Move to position after last cached token
        const_cast<Impl*>(this)->curPtr =
            getBufferPtrForSourceLoc(lastCachedToken.getRange().getEnd());
      }

      // Extend cache
      unsigned oldSize = tokenCache.size();
      tokenCache.resize(n + 8);  // Add some extra tokens

      for (unsigned i = oldSize; i < tokenCache.size() && !isAtEndOfFile(); ++i) {
        const_cast<Impl*>(this)->lexImpl();
        tokenCache[i] = nextToken;
        if (nextToken.getKind() == TokenKind::kEOF) {
          tokenCache.resize(i + 1);
          break;
        }
      }

      // Restore state
      const_cast<Impl*>(this)->curPtr = savedCurPtr;
      const_cast<Impl*>(this)->nextToken = savedNextToken;
      const_cast<Impl*>(this)->currentMode = savedMode;
    }

    if (n <= tokenCache.size()) { return tokenCache[n - 1]; }

    // Return EOF if beyond available tokens
    static Token eofToken(TokenKind::kEOF, source::SourceRange());
    return eofToken;
  }

  bool canLookAheadToken(unsigned n) const {
    if (n == 0) return true;

    initializeTokenCache();

    if (n <= tokenCache.size()) { return tokenCache[n - 1].getKind() != TokenKind::kEOF; }

    return false;
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

const Token& Lexer::lookAhead(unsigned n) const { return impl->lookAheadToken(n); }

bool Lexer::canLookAhead(unsigned n) const { return impl->canLookAheadToken(n); }

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

source::SourceLoc Lexer::getFullStartLoc() const { return source::SourceLoc(impl->triviaStartPtr); }

}  // namespace lexer
}  // namespace compiler
}  // namespace zomlang
