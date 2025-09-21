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
#include <deque>

#include "zc/core/common.h"
#include "zc/core/debug.h"
#include "zc/core/string.h"
#include "zomlang/compiler/basic/frontend.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/in-flight-diagnostic.h"
#include "zomlang/compiler/lexer/token.h"
#include "zomlang/compiler/lexer/unicode-data.h"
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
  const zc::byte* currentTokenTriviaStartPtr;

  // Token state
  Token nextToken;
  LexerMode currentMode;
  CommentRetentionMode commentMode;

  // Lookahead token cache
  mutable std::deque<Token> tokenCache;
  mutable bool cacheInitialized = false;

  // Line break tracking
  bool hasPrecedingLineBreak = false;

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
    currentTokenTriviaStartPtr = bufferStart;
    initialize();
  }

  void initialize() { lexImpl(); }

  /// Utility functions
  const zc::byte* getBufferPtrForSourceLoc(source::SourceLoc loc) const;

  void formToken(TokenKind kind, const zc::byte* tokStart, TokenFlags flags = TokenFlags::kNone) {
    source::SourceLoc startLoc = sourceMgr.getLocForOffset(bufferId, tokStart - bufferStart);
    source::SourceLoc endLoc = sourceMgr.getLocForOffset(bufferId, curPtr - bufferStart);
    zc::Maybe<zc::String> cachedText = Token::getStaticTextForTokenKind(kind);

    // For string literals and other tokens that don't have static text,
    // we need to extract the text from the source buffer
    if (cachedText == zc::none &&
        (kind == TokenKind::kStringLiteral || kind == TokenKind::kIntegerLiteral ||
         kind == TokenKind::kFloatLiteral || kind == TokenKind::kIdentifier)) {
      size_t length = curPtr - tokStart;
      zc::ArrayPtr<const zc::byte> textBytes(tokStart, length);
      cachedText = zc::str(textBytes.asChars());
    }

    // For keywords and common operators, cache the text to avoid repeated extraction
    nextToken = Token(kind, source::SourceRange(startLoc, endLoc), zc::mv(cachedText), flags);
  }

  /// Lexing implementation
  void lexImpl() {
    triviaStartPtr = curPtr;
    TokenFlags tokenFlags = TokenFlags::kNone;

    // Skip trivia and collect flags
    tokenFlags = skipTriviaAndCollectFlags();

    if (curPtr >= bufferEnd) {
      formToken(TokenKind::kEOF, curPtr, tokenFlags);
      return;
    }
    scanToken(tokenFlags);
  }

  /// Token scanning
  void scanToken(const TokenFlags& tokenFlags) {
    const zc::byte* tokStart = curPtr;

    // Check for end of file
    if (curPtr >= bufferEnd) {
      formToken(TokenKind::kEOF, tokStart, tokenFlags);
      return;
    }

    zc::byte c = *curPtr++;

    switch (c) {
      case '(':
        formToken(TokenKind::kLeftParen, tokStart, tokenFlags);
        break;
      case ')':
        formToken(TokenKind::kRightParen, tokStart, tokenFlags);
        break;
      case '{':
        formToken(TokenKind::kLeftBrace, tokStart, tokenFlags);
        break;
      case '}':
        formToken(TokenKind::kRightBrace, tokStart, tokenFlags);
        break;
      case ',':
        formToken(TokenKind::kComma, tokStart, tokenFlags);
        break;
      case ':':
        formToken(TokenKind::kColon, tokStart, tokenFlags);
        break;
      case '-':
      case '+':
      case '*':
        curPtr = tokStart;  // Reset pointer for lexOperator
        lexOperator(tokenFlags);
        break;
      case '/':
        if (curPtr < bufferEnd && *curPtr == '=') {
          curPtr++;
          formToken(TokenKind::kSlashEquals, tokStart, tokenFlags);
        } else if (curPtr < bufferEnd && *curPtr == '/') {
          lexSingleLineComment();
          return;
        } else if (curPtr < bufferEnd && *curPtr == '*') {
          lexMultiLineComment();
          return;
        } else {
          formToken(TokenKind::kSlash, tokStart, tokenFlags);
        }
        break;
      case '%':
      case '<':
        curPtr = tokStart;  // Reset pointer for lexOperator
        lexOperator(tokenFlags);
        break;
      case '>':
      case '=':
        curPtr = tokStart;  // Reset pointer for lexOperator
        lexOperator(tokenFlags);
        break;
      case '!':
      case '&':
      case '|':
      case '^':
      case '~':
      case '?':
        curPtr = tokStart;  // Reset pointer for lexOperator
        lexOperator(tokenFlags);
        break;
      case '.':
        // Check if it's a decimal number starting with a dot
        if (curPtr < bufferEnd && isdigit(*curPtr)) {
          // Go back one character since lexNumber expects to start from the first digit character
          curPtr--;
          lexNumber(tokenFlags);
        } else if (curPtr < bufferEnd && *curPtr == '.' && curPtr + 1 < bufferEnd &&
                   *(curPtr + 1) == '.') {
          curPtr += 2;
          formToken(TokenKind::kDotDotDot, tokStart, tokenFlags);
        } else {
          formToken(TokenKind::kPeriod, tokStart, tokenFlags);
        }
        break;
      case ';':
        formToken(TokenKind::kSemicolon, tokStart, tokenFlags);
        break;
      case '[':
        formToken(TokenKind::kLeftBracket, tokStart, tokenFlags);
        break;
      case ']':
        formToken(TokenKind::kRightBracket, tokStart, tokenFlags);
        break;
      case '@':
        formToken(TokenKind::kAt, tokStart, tokenFlags);
        break;
      case '#':
        formToken(TokenKind::kHash, tokStart, tokenFlags);
        break;
      case '`':
        formToken(TokenKind::kBacktick, tokStart, tokenFlags);
        break;
      default:
        // Reset curPtr to point to the current character for proper checking
        curPtr = tokStart;

        // Check if it's an identifier start character (single or multi-byte)
        uint32_t identifierStartLength = getIdentifierStartLength(c);
        if (identifierStartLength > 0) {
          lexIdentifier(tokenFlags);
        } else if (c == '\\') {
          // Unicode escape sequence at start of identifier
          if (curPtr + 1 < bufferEnd && *(curPtr + 1) == 'u') {
            lexEscapedIdentifier(tokenFlags);
          } else {
            // Invalid escape sequence - backslash not followed by 'u'
            reportInvalidCharacter('\\', tokStart);
            recoverFromInvalidCharacter();
          }
        } else if (isdigit(c)) {
          lexNumber(tokenFlags);
        } else if (c == '"') {
          lexStringLiteralImpl(tokenFlags);
        } else if (c == '\'') {
          lexSingleQuoteString(tokenFlags);
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
  TokenFlags skipTriviaAndCollectFlags() {
    TokenFlags flags = TokenFlags::kNone;

    while (curPtr < bufferEnd) {
      zc::byte c = *curPtr;
      if (c == ' ' || c == '\t' || c == '\r') {
        curPtr++;
      } else if (c == '\n') {
        flags = static_cast<TokenFlags>(static_cast<uint8_t>(flags) |
                                        static_cast<uint8_t>(TokenFlags::kPrecedingLineBreak));
        handleNewline();
      } else if (c == '/' && curPtr + 1 < bufferEnd && *(curPtr + 1) == '/') {
        lexSingleLineComment();
      } else if (c == '/' && curPtr + 1 < bufferEnd && *(curPtr + 1) == '*') {
        lexMultiLineComment();
      } else {
        break;
      }
    }

    return flags;
  }

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

  void lexIdentifier(const TokenFlags& tokenFlags) {
    // curPtr currently points to the first character of the identifier
    const zc::byte* tokStart = curPtr;

    // First, advance past the identifier start character
    zc::byte startChar = *curPtr;
    uint32_t startLength = getIdentifierStartLength(startChar);
    if (startLength > 0) {
      curPtr += startLength;
    } else {
      // This shouldn't happen if scanToken called us correctly
      curPtr++;
    }

    // Continue reading identifier characters
    while (curPtr < bufferEnd) {
      zc::byte c = *curPtr;

      // Handle escape sequences
      if (c == '\\' && curPtr + 1 < bufferEnd && *(curPtr + 1) == 'u') {
        // Unicode escape sequence: \uXXXX or \u{XXXX}
        uint32_t codePoint = lexUnicodeScalarValue();
        if (codePoint == 0) {
          // Invalid escape sequence, lexUnicodeScalarValue already reported error
          break;
        } else {
          // Validate that the code point represents a valid Unicode ID_Continue
          if (!isUnicodeIdentifierContinuation(codePoint)) {
            // Invalid Unicode escape sequence for identifier continuation
            reportInvalidCharacter('\\', curPtr - 2);  // Point to the backslash
            break;
          }
        }
      } else {
        // Check for identifier continuation character (single or multi-byte UTF-8)
        uint32_t continuationLength = getIdentifierContinuationLength(c);
        if (continuationLength > 0) {
          // Valid identifier continuation - advance by the correct number of bytes
          curPtr += continuationLength;
          continue;
        } else {
          // Not an identifier continuation character
          break;
        }
      }
    }

    // Check if it's a keyword
    auto textPtr = zc::ArrayPtr<const zc::byte>(tokStart, curPtr);

    TokenKind kind = getKeywordKind(textPtr);
    if (kind == TokenKind::kIdentifier) {
      // For identifiers, we might want to cache the text since they're frequently accessed
      source::SourceLoc startLoc = sourceMgr.getLocForOffset(bufferId, tokStart - bufferStart);
      source::SourceLoc endLoc = sourceMgr.getLocForOffset(bufferId, curPtr - bufferStart);
      zc::String identifierText = zc::str(textPtr.asChars());
      nextToken = Token(kind, source::SourceRange(startLoc, endLoc), zc::mv(identifierText));
    } else {
      formToken(kind, tokStart, tokenFlags);
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
    if (text == "bool"_zcb) return TokenKind::kBoolKeyword;
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
    if (text == "null"_zcb) return TokenKind::kNullKeyword;
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

    return TokenKind::kIdentifier;
  }

  void lexNumber(const TokenFlags& tokenFlags) {
    // The first digit character has already been consumed in scanToken
    // So we need to go back one character to include it in the token
    const zc::byte* tokStart = curPtr;  // curPtr already points to the first digit
    TokenKind kind = TokenKind::kIntegerLiteral;
    bool hasValidDigits = false;
    TokenFlags numericFlags = tokenFlags;
    bool hasSeparator = false;

    // Check for binary, octal, or hex literals
    if (tokStart >= bufferStart && *tokStart == '0' && curPtr + 1 < bufferEnd) {
      zc::byte nextChar = *(curPtr + 1);
      if (nextChar == 'b' || nextChar == 'B') {
        // Binary literal
        curPtr += 2;  // Skip '0' and 'b'/'B'
        numericFlags = static_cast<TokenFlags>(static_cast<uint16_t>(numericFlags) |
                                               static_cast<uint16_t>(TokenFlags::kBinarySpecifier));
        while (curPtr < bufferEnd && (*curPtr == '0' || *curPtr == '1' || *curPtr == '_')) {
          if (*curPtr == '_') {
            hasSeparator = true;
          } else {
            hasValidDigits = true;
          }
          curPtr++;
        }
        if (hasSeparator) {
          numericFlags =
              static_cast<TokenFlags>(static_cast<uint16_t>(numericFlags) |
                                      static_cast<uint16_t>(TokenFlags::kContainsSeparator));
        }
        if (!hasValidDigits) { reportInvalidNumberLiteral("binary"_zc, tokStart); }
        formToken(TokenKind::kIntegerLiteral, tokStart, numericFlags);
        return;
      } else if (nextChar == 'o' || nextChar == 'O') {
        // Octal literal
        curPtr += 2;  // Skip '0' and 'o'/'O'
        numericFlags = static_cast<TokenFlags>(static_cast<uint16_t>(numericFlags) |
                                               static_cast<uint16_t>(TokenFlags::kOctalSpecifier));
        while (curPtr < bufferEnd && ((*curPtr >= '0' && *curPtr <= '7') || *curPtr == '_')) {
          if (*curPtr == '_') {
            hasSeparator = true;
          } else {
            hasValidDigits = true;
          }
          curPtr++;
        }
        if (hasSeparator) {
          numericFlags =
              static_cast<TokenFlags>(static_cast<uint16_t>(numericFlags) |
                                      static_cast<uint16_t>(TokenFlags::kContainsSeparator));
        }
        if (!hasValidDigits) { reportInvalidNumberLiteral("octal"_zc, tokStart); }
        formToken(TokenKind::kIntegerLiteral, tokStart, numericFlags);
        return;
      } else if (nextChar == 'x' || nextChar == 'X') {
        // Hexadecimal literal
        curPtr += 2;  // Skip '0' and 'x'/'X'
        numericFlags = static_cast<TokenFlags>(static_cast<uint16_t>(numericFlags) |
                                               static_cast<uint16_t>(TokenFlags::kHexSpecifier));
        while (curPtr < bufferEnd && (isdigit(*curPtr) || (*curPtr >= 'a' && *curPtr <= 'f') ||
                                      (*curPtr >= 'A' && *curPtr <= 'F') || *curPtr == '_')) {
          if (*curPtr == '_') {
            hasSeparator = true;
          } else {
            hasValidDigits = true;
          }
          curPtr++;
        }
        if (hasSeparator) {
          numericFlags =
              static_cast<TokenFlags>(static_cast<uint16_t>(numericFlags) |
                                      static_cast<uint16_t>(TokenFlags::kContainsSeparator));
        }
        if (!hasValidDigits) { reportInvalidNumberLiteral("hexadecimal"_zc, tokStart); }
        formToken(TokenKind::kIntegerLiteral, tokStart, numericFlags);
        return;
      }
    }

    // Read decimal integer part (allowing numeric separators)
    curPtr++;  // Skip the first digit that was already consumed
    while (curPtr < bufferEnd && (isdigit(*curPtr) || *curPtr == '_')) {
      if (*curPtr == '_') {
        hasSeparator = true;
      } else {
        hasValidDigits = true;
      }
      curPtr++;
    }

    // Check if it's a floating-point number
    if (curPtr < bufferEnd && *curPtr == '.' && curPtr + 1 < bufferEnd && isdigit(*(curPtr + 1))) {
      curPtr++;  // Skip '.'
      while (curPtr < bufferEnd && (isdigit(*curPtr) || *curPtr == '_')) {
        if (*curPtr == '_') { hasSeparator = true; }
        curPtr++;
      }
      kind = TokenKind::kFloatLiteral;
    }

    // Check for exponent part
    if (curPtr < bufferEnd && (*curPtr == 'e' || *curPtr == 'E')) {
      curPtr++;  // Skip 'e' or 'E'
      numericFlags = static_cast<TokenFlags>(static_cast<uint16_t>(numericFlags) |
                                             static_cast<uint16_t>(TokenFlags::kScientific));
      if (curPtr < bufferEnd && (*curPtr == '+' || *curPtr == '-')) {
        curPtr++;  // Skip sign
      }
      const zc::byte* expStart = curPtr;
      while (curPtr < bufferEnd && (isdigit(*curPtr) || *curPtr == '_')) {
        if (*curPtr == '_') { hasSeparator = true; }
        curPtr++;
      }
      // Check if exponent has digits
      if (curPtr == expStart) { reportInvalidNumberLiteral("exponent"_zc, tokStart); }
      kind = TokenKind::kFloatLiteral;
    }

    if (hasSeparator) {
      numericFlags = static_cast<TokenFlags>(static_cast<uint16_t>(numericFlags) |
                                             static_cast<uint16_t>(TokenFlags::kContainsSeparator));
    }

    // Handle numeric literal suffixes
    if (curPtr < bufferEnd) {
      zc::byte c = *curPtr;
      if (c == 'u' || c == 'U') {
        curPtr++;  // Skip 'u' or 'U'
        if (curPtr < bufferEnd && (*curPtr == 'l' || *curPtr == 'L')) {
          curPtr++;  // Skip 'l' or 'L' for 'ul' suffix
        }
      } else if (c == 'l' || c == 'L') {
        curPtr++;  // Skip 'l' or 'L'
        if (curPtr < bufferEnd && (*curPtr == 'u' || *curPtr == 'U')) {
          curPtr++;  // Skip 'u' or 'U' for 'lu' suffix
        }
      } else if (c == 'f' || c == 'F') {
        curPtr++;  // Skip 'f' or 'F'
        kind = TokenKind::kFloatLiteral;
      } else if (c == 'd' || c == 'D') {
        curPtr++;  // Skip 'd' or 'D'
        kind = TokenKind::kFloatLiteral;
      }
    }

    formToken(kind, tokStart, numericFlags);
  }
  void lexStringLiteralImpl(const TokenFlags& tokenFlags) {
    // curPtr currently points to the quote character due to curPtr-- in scanToken
    // tokStart should point to the quote character for the entire token
    const zc::byte* tokStart = curPtr;
    // Remember which quote character we're using
    zc::byte quoteChar = *(tokStart);
    bool foundClosingQuote = false;

    // Skip the starting quote
    curPtr++;

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
        if (escaped == 'u') {
          // Handle Unicode escape sequence
          lexUnicodeScalarValue();
        } else {
          // Validate common escape sequences
          if (escaped != 'n' && escaped != 't' && escaped != 'r' && escaped != '\\' &&
              escaped != '"' && escaped != '\'' && escaped != '0' && escaped != 'x') {}
        }
        // Note: We continue parsing after escape sequences, including \n
      } else if (c == '\n' || c == '\r') {
        // Allow actual newline characters in string literals
        // Continue parsing - newlines are valid content in string literals
      }
    }

    // Check if we reached end of file without closing quote
    if (!foundClosingQuote && curPtr >= bufferEnd) { reportUnterminatedString(tokStart); }

    formToken(TokenKind::kStringLiteral, tokStart, tokenFlags);
  }

  void lexSingleQuoteString(const TokenFlags& tokenFlags) {
    // curPtr currently points to the quote character due to curPtr-- in scanToken
    // tokStart should point to the quote character for the entire token
    const zc::byte* tokStart = curPtr;
    bool foundClosingQuote = false;
    size_t charCount = 0;

    // Skip the starting quote
    curPtr++;
    while (curPtr < bufferEnd) {
      zc::byte c = *curPtr++;
      if (c == '\'') {
        // Found the ending quote
        foundClosingQuote = true;
        break;
      } else if (c == '\\' && curPtr < bufferEnd) {
        // Handle escape character
        zc::byte escaped = *curPtr++;
        if (escaped == 'u') {
          // Handle Unicode escape sequence
          lexUnicodeScalarValue();
        } else {
          // Validate common escape sequences
          if (escaped != 'n' && escaped != 't' && escaped != 'r' && escaped != '\\' &&
              escaped != '"' && escaped != '\'' && escaped != '0' && escaped != 'x') {
            reportInvalidEscapeSequence(escaped, curPtr - 2);
          }
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

    formToken(TokenKind::kCharacterLiteral, tokStart, tokenFlags);
  }

  void lexEscapedIdentifier(const TokenFlags& tokenFlags) {
    const zc::byte* tokStart = curPtr;

    // Handle Unicode escape sequence at start of identifier
    if (*curPtr == '\\' && curPtr + 1 < bufferEnd && *(curPtr + 1) == 'u') {
      uint32_t codePoint = lexUnicodeScalarValue();
      if (codePoint == 0) {
        // Invalid escape sequence, lexUnicodeScalarValue already reported error
        // Continue lexing as a regular identifier starting from the backslash
        curPtr = tokStart + 1;
      } else {
        // Validate that the code point represents a valid Unicode ID_Start
        if (!isUnicodeIdentifierStart(codePoint)) {
          // Invalid Unicode escape sequence for identifier start
          reportInvalidCharacter('\\', tokStart);  // Point to the backslash
          curPtr = tokStart + 1;                   // Skip the backslash and continue
        }
      }
    }

    // Continue reading identifier parts
    while (curPtr < bufferEnd) {
      zc::byte c = *curPtr;

      // Handle escape sequences
      if (c == '\\' && curPtr + 1 < bufferEnd && *(curPtr + 1) == 'u') {
        uint32_t codePoint = lexUnicodeScalarValue();
        if (codePoint == 0) {
          // Invalid escape sequence, lexUnicodeScalarValue already reported error
          break;
        } else {
          // Validate that the code point represents a valid Unicode ID_Continue
          if (!isUnicodeIdentifierContinuation(codePoint)) {
            // Invalid Unicode escape sequence for identifier continuation
            reportInvalidCharacter('\\', curPtr - 2);  // Point to the backslash
            break;
          }
        }
      } else {
        // Check for identifier continuation character (single or multi-byte UTF-8)
        uint32_t continuationLength = getIdentifierContinuationLength(c);
        if (continuationLength > 0) {
          // Valid identifier continuation - advance by the correct number of bytes
          curPtr += continuationLength;
        } else {
          // Not an identifier continuation character
          break;
        }
      }
    }

    // Check if it's a keyword
    auto textPtr = zc::ArrayPtr<const zc::byte>(tokStart, curPtr);
    TokenKind kind = getKeywordKind(textPtr);

    if (kind == TokenKind::kIdentifier) {
      // For identifiers, we might want to cache the text since they're frequently accessed
      source::SourceLoc startLoc = sourceMgr.getLocForOffset(bufferId, tokStart - bufferStart);
      source::SourceLoc endLoc = sourceMgr.getLocForOffset(bufferId, curPtr - bufferStart);
      zc::String identifierText = zc::str(textPtr.asChars());
      nextToken =
          Token(kind, source::SourceRange(startLoc, endLoc), zc::mv(identifierText), tokenFlags);
    } else {
      formToken(kind, tokStart, tokenFlags);
    }
  }

  void lexOperator(const TokenFlags& tokenFlags) {
    const zc::byte* tokStart = curPtr;
    zc::byte c = *curPtr++;

    // Handle multi-character operators
    switch (c) {
      case '=':
        if (curPtr < bufferEnd && *curPtr == '=') {
          curPtr++;
          if (curPtr < bufferEnd && *curPtr == '=') {
            curPtr++;
            formToken(TokenKind::kEqualsEqualsEquals, tokStart, tokenFlags);
          } else {
            formToken(TokenKind::kEqualsEquals, tokStart, tokenFlags);
          }
        } else if (curPtr < bufferEnd && *curPtr == '>') {
          curPtr++;
          formToken(TokenKind::kEqualsGreaterThan, tokStart, tokenFlags);
        } else {
          formToken(TokenKind::kEquals, tokStart, tokenFlags);
        }
        break;
      case '!':
        if (curPtr < bufferEnd && *curPtr == '=') {
          curPtr++;
          if (curPtr < bufferEnd && *curPtr == '=') {
            curPtr++;
            formToken(TokenKind::kExclamationEqualsEquals, tokStart, tokenFlags);
          } else {
            formToken(TokenKind::kExclamationEquals, tokStart, tokenFlags);
          }
        } else if (curPtr < bufferEnd && *curPtr == '!') {
          curPtr++;
          formToken(TokenKind::kErrorUnwrap, tokStart, tokenFlags);
        } else {
          formToken(TokenKind::kExclamation, tokStart, tokenFlags);
        }
        break;
      case '<':
        if (curPtr < bufferEnd && *curPtr == '=') {
          curPtr++;
          formToken(TokenKind::kLessThanEquals, tokStart, tokenFlags);
        } else if (curPtr < bufferEnd && *curPtr == '<') {
          curPtr++;
          if (curPtr < bufferEnd && *curPtr == '=') {
            curPtr++;
            formToken(TokenKind::kLessThanLessThanEquals, tokStart, tokenFlags);
          } else {
            formToken(TokenKind::kLessThanLessThan, tokStart, tokenFlags);
          }
        } else {
          formToken(TokenKind::kLessThan, tokStart, tokenFlags);
        }
        break;
      case '>':
        if (curPtr < bufferEnd && *curPtr == '=') {
          curPtr++;
          formToken(TokenKind::kGreaterThanEquals, tokStart, tokenFlags);
        } else if (curPtr < bufferEnd && *curPtr == '>') {
          curPtr++;
          if (curPtr < bufferEnd && *curPtr == '=') {
            curPtr++;
            formToken(TokenKind::kGreaterThanGreaterThanEquals, tokStart, tokenFlags);
          } else if (curPtr < bufferEnd && *curPtr == '>') {
            curPtr++;
            if (curPtr < bufferEnd && *curPtr == '=') {
              curPtr++;
              formToken(TokenKind::kGreaterThanGreaterThanGreaterThanEquals, tokStart, tokenFlags);
            } else {
              formToken(TokenKind::kGreaterThanGreaterThanGreaterThan, tokStart, tokenFlags);
            }
          } else {
            formToken(TokenKind::kGreaterThanGreaterThan, tokStart, tokenFlags);
          }
        } else {
          formToken(TokenKind::kGreaterThan, tokStart, tokenFlags);
        }
        break;
      case '&':
        if (curPtr < bufferEnd && *curPtr == '&') {
          curPtr++;
          if (curPtr < bufferEnd && *curPtr == '=') {
            curPtr++;
            formToken(TokenKind::kAmpersandAmpersandEquals, tokStart, tokenFlags);
          } else {
            formToken(TokenKind::kAmpersandAmpersand, tokStart, tokenFlags);
          }
        } else if (curPtr < bufferEnd && *curPtr == '=') {
          curPtr++;
          formToken(TokenKind::kAmpersandEquals, tokStart, tokenFlags);
        } else {
          formToken(TokenKind::kAmpersand, tokStart, tokenFlags);
        }
        break;
      case '|':
        if (curPtr < bufferEnd && *curPtr == '|') {
          curPtr++;
          if (curPtr < bufferEnd && *curPtr == '=') {
            curPtr++;
            formToken(TokenKind::kBarBarEquals, tokStart, tokenFlags);
          } else {
            formToken(TokenKind::kBarBar, tokStart, tokenFlags);
          }
        } else if (curPtr < bufferEnd && *curPtr == '=') {
          curPtr++;
          formToken(TokenKind::kBarEquals, tokStart, tokenFlags);
        } else {
          formToken(TokenKind::kBar, tokStart, tokenFlags);
        }
        break;
      case '^':
        if (curPtr < bufferEnd && *curPtr == '=') {
          curPtr++;
          formToken(TokenKind::kCaretEquals, tokStart, tokenFlags);
        } else {
          formToken(TokenKind::kCaret, tokStart, tokenFlags);
        }
        break;
      case '+':
        if (curPtr < bufferEnd && *curPtr == '+') {
          curPtr++;
          formToken(TokenKind::kPlusPlus, tokStart, tokenFlags);
        } else if (curPtr < bufferEnd && *curPtr == '=') {
          curPtr++;
          formToken(TokenKind::kPlusEquals, tokStart, tokenFlags);
        } else {
          formToken(TokenKind::kPlus, tokStart, tokenFlags);
        }
        break;
      case '-':
        if (curPtr < bufferEnd && *curPtr == '>') {
          curPtr++;
          formToken(TokenKind::kArrow, tokStart, tokenFlags);
        } else if (curPtr < bufferEnd && *curPtr == '-') {
          curPtr++;
          formToken(TokenKind::kMinusMinus, tokStart, tokenFlags);
        } else if (curPtr < bufferEnd && *curPtr == '=') {
          curPtr++;
          formToken(TokenKind::kMinusEquals, tokStart, tokenFlags);
        } else {
          formToken(TokenKind::kMinus, tokStart, tokenFlags);
        }
        break;
      case '*':
        if (curPtr < bufferEnd && *curPtr == '*') {
          curPtr++;
          if (curPtr < bufferEnd && *curPtr == '=') {
            curPtr++;
            formToken(TokenKind::kAsteriskAsteriskEquals, tokStart, tokenFlags);
          } else {
            formToken(TokenKind::kAsteriskAsterisk, tokStart, tokenFlags);
          }
        } else if (curPtr < bufferEnd && *curPtr == '=') {
          curPtr++;
          formToken(TokenKind::kAsteriskEquals, tokStart, tokenFlags);
        } else {
          formToken(TokenKind::kAsterisk, tokStart, tokenFlags);
        }
        break;
      case '/':
        if (curPtr < bufferEnd && *curPtr == '=') {
          curPtr++;
          formToken(TokenKind::kSlashEquals, tokStart, tokenFlags);
        } else {
          formToken(TokenKind::kSlash, tokStart, tokenFlags);
        }
        break;
      case '%':
        if (curPtr < bufferEnd && *curPtr == '=') {
          curPtr++;
          formToken(TokenKind::kPercentEquals, tokStart, tokenFlags);
        } else {
          formToken(TokenKind::kPercent, tokStart, tokenFlags);
        }
        break;
      case '?':
        if (curPtr < bufferEnd && *curPtr == '?') {
          curPtr++;
          if (curPtr < bufferEnd && *curPtr == '=') {
            curPtr++;
            formToken(TokenKind::kQuestionQuestionEquals, tokStart, tokenFlags);
          } else {
            formToken(TokenKind::kQuestionQuestion, tokStart, tokenFlags);
          }
        } else if (curPtr < bufferEnd && *curPtr == '.') {
          curPtr++;
          formToken(TokenKind::kQuestionDot, tokStart, tokenFlags);
        } else if (curPtr < bufferEnd && *curPtr == '!') {
          curPtr++;
          formToken(TokenKind::kErrorPropagate, tokStart, tokenFlags);
        } else if (curPtr < bufferEnd && *curPtr == ':') {
          curPtr++;
          formToken(TokenKind::kErrorDefault, tokStart, tokenFlags);
        } else {
          formToken(TokenKind::kQuestion, tokStart, tokenFlags);
        }
        break;
      case '.':
        if (curPtr < bufferEnd && *curPtr == '.' && curPtr + 1 < bufferEnd &&
            *(curPtr + 1) == '.') {
          curPtr += 2;
          formToken(TokenKind::kDotDotDot, tokStart, tokenFlags);
        } else {
          formToken(TokenKind::kPeriod, tokStart, tokenFlags);
        }
        break;
      default:
        // Single character operator, should have been handled in scanToken
        reportInvalidCharacter(c, tokStart);
        recoverFromInvalidCharacter();
        break;
    }
  }

  /// Unicode handling
  uint32_t lexUnicodeScalarValue() {
    // Expect to be positioned at the start of a Unicode escape sequence
    // This function handles both \uXXXX and \u{XXXX} formats

    if (curPtr >= bufferEnd || *curPtr != '\\') {
      return 0;  // Invalid: not a backslash
    }

    const zc::byte* start = curPtr;
    curPtr++;  // Skip '\\'

    if (curPtr >= bufferEnd || *curPtr != 'u') {
      curPtr = start;  // Restore position
      return 0;        // Invalid: not followed by 'u'
    }

    curPtr++;  // Skip 'u'

    // Check for extended Unicode escape \u{XXXX}
    if (curPtr < bufferEnd && *curPtr == '{') {
      curPtr++;  // Skip '{'

      uint32_t value = 0;
      int digitCount = 0;

      // Scan hex digits
      while (curPtr < bufferEnd && isxdigit(*curPtr) && digitCount < 6) {
        zc::byte c = *curPtr;
        uint32_t digit;
        if (c >= '0' && c <= '9') {
          digit = c - '0';
        } else if (c >= 'A' && c <= 'F') {
          digit = c - 'A' + 10;
        } else if (c >= 'a' && c <= 'f') {
          digit = c - 'a' + 10;
        } else {
          break;
        }

        value = (value << 4) | digit;
        curPtr++;
        digitCount++;
      }

      // Must have at least one hex digit
      if (digitCount == 0) {
        curPtr = start;  // Restore position
        return 0;        // Invalid: no hex digits
      }

      // Must be followed by '}'
      if (curPtr >= bufferEnd || *curPtr != '}') {
        curPtr = start;  // Restore position
        return 0;        // Invalid: missing closing brace
      }

      curPtr++;  // Skip '}'

      // Validate Unicode scalar value range
      if (value > 0x10FFFF) {
        curPtr = start;  // Restore position
        return 0;        // Invalid: out of Unicode range
      }

      return value;
    }

    // Handle standard Unicode escape \uXXXX
    uint32_t value = 0;
    for (int i = 0; i < 4; i++) {
      if (curPtr >= bufferEnd || !isxdigit(*curPtr)) {
        curPtr = start;  // Restore position
        return 0;        // Invalid: insufficient hex digits
      }

      zc::byte c = *curPtr;
      uint32_t digit;
      if (c >= '0' && c <= '9') {
        digit = c - '0';
      } else if (c >= 'A' && c <= 'F') {
        digit = c - 'A' + 10;
      } else if (c >= 'a' && c <= 'f') {
        digit = c - 'a' + 10;
      } else {
        curPtr = start;  // Restore position
        return 0;        // Invalid hex digit
      }

      value = (value << 4) | digit;
      curPtr++;
    }

    return value;
  }

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
      if (*curPtr == '\n') { hasPrecedingLineBreak = true; }
      curPtr++;
    }

    // Check if we reached end of file without closing comment
    if (curPtr + 1 >= bufferEnd &&
        !(curPtr < bufferEnd && *curPtr == '*' && curPtr + 1 < bufferEnd && *(curPtr + 1) == '/')) {
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
  /// Check if a byte is the start of an identifier character
  /// Returns the number of bytes consumed (0 if not an identifier start)
  uint32_t getIdentifierStartLength(zc::byte c) const {
    // First check ASCII cases: letters, underscore, and dollar sign
    if (isalpha(c) || c == '_' || c == '$') { return 1; }

    // For non-ASCII characters, decode UTF-8 and check Unicode properties
    if ((c & 0x80) != 0) {
      // Use curPtr to read the complete UTF-8 sequence from the buffer
      const zc::byte* ptr = curPtr;
      uint32_t codePoint = 0;
      if (uint32_t offset = tryDecodeUtf8CodePoint(ptr, codePoint)) {
        if (isUnicodeIdentifierStart(codePoint)) { return offset; }
      }
    }

    return 0;
  }

  /// Check if a byte is the continuation of an identifier character
  /// Returns the number of bytes consumed (0 if not an identifier continuation)
  uint32_t getIdentifierContinuationLength(zc::byte c) const {
    // First check ASCII cases: letters, digits, underscore, and dollar sign
    if (isalnum(c) || c == '_' || c == '$') { return 1; }

    // For non-ASCII characters, decode UTF-8 and check Unicode properties
    if ((c & 0x80) != 0) {
      // Use curPtr to read the complete UTF-8 sequence from the buffer
      const zc::byte* ptr = curPtr;
      uint32_t codePoint = 0;
      if (uint32_t offset = tryDecodeUtf8CodePoint(ptr, codePoint)) {
        // Check for ZWNJ (U+200C) and ZWJ (U+200D) as per grammar rules
        if (codePoint == 0x200C || codePoint == 0x200D) { return offset; }
        if (isUnicodeIdentifierContinuation(codePoint)) { return offset; }
      }
    }

    return 0;
  }

  /// Legacy helper functions for backward compatibility
  bool isIdentifierStart(zc::byte c) const { return getIdentifierStartLength(c) > 0; }

  bool isIdentifierContinuation(zc::byte c) const { return getIdentifierContinuationLength(c) > 0; }

  /// UTF-8 decoding helper
  uint32_t tryDecodeUtf8CodePoint(const zc::byte* ptr, uint32_t& codePoint) const {
    if (ptr >= bufferEnd) { return 0; }

    zc::byte c = *ptr;

    if (c < 0x80) {
      // ASCII - single byte
      codePoint = c;
      return 1;
    }

    if ((c & 0xe0) == 0xc0) {
      // 2-byte sequence: 110xxxxx 10xxxxxx
      if (ptr + 1 >= bufferEnd) { return 0; }
      zc::byte c2 = ptr[1];
      if ((c2 & 0xc0) != 0x80) { return 0; }

      codePoint = ((c & 0x1f) << 6) | (c2 & 0x3f);
      // Check for overlong encoding
      if (codePoint < 0x80) { return 0; }

      return 2;
    }

    if ((c & 0xf0) == 0xe0) {
      // 3-byte sequence: 1110xxxx 10xxxxxx 10xxxxxx
      if (ptr + 2 >= bufferEnd) { return 0; }
      zc::byte c2 = ptr[1];
      zc::byte c3 = ptr[2];
      if ((c2 & 0xc0) != 0x80 || (c3 & 0xc0) != 0x80) { return 0; }

      codePoint = ((c & 0x0f) << 12) | ((c2 & 0x3f) << 6) | (c3 & 0x3f);
      // Check for overlong encoding
      if (codePoint < 0x800) { return 0; }
      // Check for surrogate pairs (invalid in UTF-8)
      if (codePoint >= 0xd800 && codePoint <= 0xdfff) { return 0; }

      return 3;
    }

    if ((c & 0xf8) == 0xf0) {
      // 4-byte sequence: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
      if (ptr + 3 >= bufferEnd) { return 0; }
      zc::byte c2 = ptr[1];
      zc::byte c3 = ptr[2];
      zc::byte c4 = ptr[3];
      if ((c2 & 0xc0) != 0x80 || (c3 & 0xc0) != 0x80 || (c4 & 0xc0) != 0x80) { return 0; }

      codePoint = ((c & 0x07) << 18) | ((c2 & 0x3f) << 12) | ((c3 & 0x3f) << 6) | (c4 & 0x3f);
      // Check for overlong encoding
      if (codePoint < 0x10000) { return 0; }
      // Check for valid Unicode range
      if (codePoint > 0x10ffff) { return 0; }

      return 4;
    }

    // Invalid UTF-8 start byte
    return 0;
  }

  /// Unicode identifier support
  bool isUnicodeIdentifierStart(const uint32_t codePoint) const { return isIdStart(codePoint); }

  bool isUnicodeIdentifierContinuation(uint32_t codePoint) const { return isIdContinue(codePoint); }

  bool isOperatorStart(zc::byte c) const {
    return c == '+' || c == '-' || c == '*' || c == '/' || c == '=' || c == '<' || c == '>' ||
           c == '!' || c == '&' || c == '|';
  }

  /// \brief Shift the lookahead cache by removing the first token
  /// This maintains cache consistency without invalidating the entire cache
  void shiftTokenCache() {
    if (cacheInitialized && !tokenCache.empty()) { tokenCache.pop_front(); }
  }

  /// Lookahead functionality
  void initializeTokenCache() {
    if (cacheInitialized) { return; }

    // Save current lexer state
    const zc::byte* savedCurPtr = curPtr;
    Token savedNextToken = nextToken;
    LexerMode savedMode = currentMode;

    // Create a temporary lexer state for lookahead
    curPtr = savedCurPtr;
    currentMode = savedMode;

    // Pre-lex some tokens for lookahead
    const unsigned kInitialCacheSize = 16;

    for (unsigned i = 0; i < kInitialCacheSize && !isAtEndOfFile(); ++i) {
      lexImpl();
      tokenCache.push_back(nextToken);
    }

    // Restore original lexer state
    curPtr = savedCurPtr;
    nextToken = savedNextToken;
    currentMode = savedMode;

    cacheInitialized = true;
  }

  const Token& lookAheadToken(unsigned n) {
    ZC_REQUIRE(n > 0, "lookAheadToken: n must be greater than 0");

    if (n == 1) { return nextToken; }

    initializeTokenCache();

    // Cache index: n == 2 -> index 0, n == 3 -> index 1, etc.
    unsigned cacheIndex = n - 2;

    if (cacheIndex < tokenCache.size()) { return tokenCache[cacheIndex]; }

    // Extend cache if needed

    // Save current state
    const zc::byte* savedCurPtr = curPtr;
    Token savedNextToken = nextToken;
    LexerMode savedMode = currentMode;

    // Position lexer at the end of cached tokens
    if (!tokenCache.empty()) {
      const Token& lastCachedToken = tokenCache.back();
      if (lastCachedToken.getKind() == TokenKind::kEOF) { return lastCachedToken; }
      // Move to position after last cached token
      curPtr = getBufferPtrForSourceLoc(lastCachedToken.getRange().getEnd());
    }

    // Extend cache
    unsigned tokensNeeded = (n - 2) - tokenCache.size() + 1 + 8;  // Add some extra tokens

    for (unsigned i = 0; i < tokensNeeded && !isAtEndOfFile(); ++i) {
      lexImpl();
      tokenCache.push_back(nextToken);
      if (nextToken.getKind() == TokenKind::kEOF) { break; }
    }

    // Restore state
    curPtr = savedCurPtr;
    nextToken = savedNextToken;
    currentMode = savedMode;

    // Return the requested token from cache
    if (cacheIndex < tokenCache.size()) { return tokenCache[cacheIndex]; }

    // Return EOF if beyond available tokens
    static Token eofToken(TokenKind::kEOF, source::SourceRange());
    return eofToken;
  }

  bool canLookAheadToken(unsigned n) {
    ZC_REQUIRE(n > 0, "canLookAheadToken: n must be greater than 0");

    if (n == 1) { return nextToken.getKind() != TokenKind::kEOF; }

    initializeTokenCache();

    // Cache index: n == 2 -> index 0, n == 3 -> index 1, etc.
    unsigned cacheIndex = n - 2;
    if (cacheIndex < tokenCache.size()) {
      return tokenCache[cacheIndex].getKind() != TokenKind::kEOF;
    }

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
  impl->currentTokenTriviaStartPtr = impl->triviaStartPtr;

  impl->shiftTokenCache();

  impl->lexImpl();
}

const Token& Lexer::peekNextToken() const { return impl->nextToken; }

const Token& Lexer::lookAhead(unsigned n) { return impl->lookAheadToken(n); }

bool Lexer::canLookAhead(unsigned n) { return impl->canLookAheadToken(n); }

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

bool Lexer::tryLexRegexLiteral(const zc::byte* tokStart) {
  // Original implementation...
  return false;
}

void Lexer::lexStringLiteral(unsigned customDelimiterLen) {
  impl->lexStringLiteralImpl(TokenFlags::kNone);
}

bool Lexer::isCodeCompletion() const { return impl->triviaStartPtr >= impl->bufferEnd; }

void Lexer::setCommentRetentionMode(CommentRetentionMode mode) { impl->commentMode = mode; }

source::SourceLoc Lexer::getLocForStartOfToken(source::SourceLoc loc) const {
  if (loc.isInvalid()) { return {}; }
  return {};
}

source::CharSourceRange Lexer::getCharSourceRangeFromSourceRange(
    const source::SourceRange& sr) const {
  return source::CharSourceRange(sr.getStart(), sr.getEnd());
}

source::SourceLoc Lexer::getFullStartLoc() const {
  return source::SourceLoc(impl->currentTokenTriviaStartPtr);
}

}  // namespace lexer
}  // namespace compiler
}  // namespace zomlang
