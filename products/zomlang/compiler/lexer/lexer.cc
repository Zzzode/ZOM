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
#include <cstdint>
#include <cstdlib>

#include "zc/core/common.h"
#include "zc/core/debug.h"
#include "zc/core/string.h"
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/kinds.h"
#include "zomlang/compiler/basic/frontend.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/diagnostics/diagnostic-ids.h"
#include "zomlang/compiler/diagnostics/in-flight-diagnostic.h"
#include "zomlang/compiler/lexer/bigint.h"
#include "zomlang/compiler/lexer/token.h"
#include "zomlang/compiler/lexer/utils.h"
#include "zomlang/compiler/source/location.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang {
namespace compiler {
namespace lexer {

enum class EscapeSequenceScanningFlags : uint8_t {
  String = 1 << 0,
  ReportErrors = 1 << 1,

  RegularExpression = 1 << 2,
  AnnexB = 1 << 3,
  AnyUnicodeMode = 1 << 4,
  AtomEscape = 1 << 5,

  ReportInvalidEscapeErrors = RegularExpression | ReportErrors,
  AllowExtendedUnicodeEscape = String | AnyUnicodeMode,
};

constexpr EscapeSequenceScanningFlags operator|(EscapeSequenceScanningFlags a,
                                                EscapeSequenceScanningFlags b) {
  return static_cast<EscapeSequenceScanningFlags>(static_cast<uint8_t>(a) |
                                                  static_cast<uint8_t>(b));
}

constexpr EscapeSequenceScanningFlags operator&(EscapeSequenceScanningFlags a,
                                                EscapeSequenceScanningFlags b) {
  return static_cast<EscapeSequenceScanningFlags>(static_cast<uint8_t>(a) &
                                                  static_cast<uint8_t>(b));
}

constexpr EscapeSequenceScanningFlags operator|=(EscapeSequenceScanningFlags& a,
                                                 EscapeSequenceScanningFlags b) {
  return a = a | b;
}

constexpr bool hasFlag(EscapeSequenceScanningFlags flags, EscapeSequenceScanningFlags flag) {
  return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
}

struct Lexer::Impl {
  /// Reference members
  const source::SourceManager& sourceMgr;
  /// Diagnostic engine for reporting errors
  diagnostics::DiagnosticEngine& diagnosticEngine;
  /// Language options
  const basic::LangOptions& langOpts;
  /// String pool for interning strings
  basic::StringPool& stringPool;
  /// Buffer ID for the buffer being lexed
  const source::BufferId& bufferId;
  /// Buffer bounds
  const zc::byte* bufferStart;
  /// End of text
  const zc::byte* bufferEnd;
  /// Embedded lexer state
  LexerState state;

  /// Collected comment directives
  zc::Vector<CommentDirective> commentDirectives;

  Impl(const source::SourceManager& sourceMgr, diagnostics::DiagnosticEngine& diagnosticEngine,
       const basic::LangOptions& options, basic::StringPool& stringPool,
       const source::BufferId& bufferId);

  /// \brief Get the current byte at the cursor position.
  /// \note Even though this returns a rune (int32_t), it only decodes the current byte. It must be
  /// checked against < 0x80 to verify that a call to charWithSize is not needed.
  /// \return The current byte at the cursor position.
  int32_t ch() const;

  /// \brief Get the character at the specified offset from the current position.
  /// \param offset The offset from the current cursor position.
  int32_t charAt(int32_t offset) const;

  /// \brief Get the current character and its size in bytes. This handles multi-byte UTF-8
  /// sequences correctly.
  /// \return A pair containing the decoded character (rune) and its size in bytes.
  std::pair<uint32_t, uint32_t> charWithSize() const;

  /// \brief Form a token of the given kind and advance the lexer.
  /// \param kind The syntax kind of the token.
  /// \param value Optional string value for the token (e.g., for identifiers or literals).
  void formToken(ast::SyntaxKind kind, zc::Maybe<zc::StringPtr> value = zc::none);

  /// \brief Process a comment directive.
  /// \param start The start of the comment directive.
  /// \param end The end of the comment directive.
  /// \param isMultiLine Whether the comment is multi-line.
  void processCommentDirective(const zc::byte* start, const zc::byte* end, bool isMultiLine);

  /// \brief Lex an identifier token.
  /// \param text The text of the identifier.
  void getIdentifierToken(zc::StringPtr text);

  /// \brief Lex hexadecimal digits.
  /// \param minCount The minimum number of digits to lex.
  /// \param scanAsManyAsPossible Whether to scan as many digits as possible.
  /// \param canHaveSeparators Whether the digits can have separators.
  /// \return The lexed hexadecimal digits as a string.
  zc::StringPtr lexHexDigits(int32_t minCount, bool scanAsManyAsPossible, bool canHaveSeparators);

  /// \brief Lex binary or octal digits.
  /// \param base The base of the number (2 for binary, 8 for octal).
  /// \return The lexed digits as a string.
  zc::StringPtr lexBinaryOrOctalDigits(int32_t base);

  void lex();

  /// \brief Lex an identifier.
  /// \param prefixLength The length of the prefix (e.g., '$' for a global identifier).
  /// \return The lexed identifier as a string.
  zc::Maybe<zc::StringPtr> lexIdentifier(int32_t prefixLength);

  /// \brief Peek at the next Unicode escape sequence.
  /// \return The Unicode code point if a valid escape sequence is found, -1 otherwise.
  int32_t peekUnicodeEscape();

  /// \brief Lex a number.
  void lexNumber();

  /// \brief Lex a number fragment.
  /// \return The lexed number fragment as a string.
  zc::StringPtr lexNumberFragment();

  /// \brief Lex digits.
  /// \return A pair containing the lexed digits as a string and a boolean indicating if a decimal
  /// point was found.
  std::pair<zc::StringPtr, bool> lexDigits();

  /// \brief Lex a big int suffix.
  /// \param tokenValue The token value to check.
  /// \return The syntax kind of the big int suffix.
  ast::SyntaxKind lexBigIntSuffix(zc::StringPtr& tokenValue);

  /// \brief Set the tokenValue and returns a NoSubstitutionTemplateLiteral or a literal component
  /// of a TemplateExpression.
  /// @param shouldEmitInvalidEscapeError Whether to emit an error for an invalid escape sequence.
  /// @param outValue The token value to set.
  /// @return The syntax kind of the template literal.
  ast::SyntaxKind lexTemplateAndRetTokenValue(bool shouldEmitInvalidEscapeError,
                                              zc::StringPtr& outValue);

  /// \brief Lex identifier parts.
  /// \return The lexed identifier parts as a string.
  zc::StringPtr lexIdentifierParts();

  /// \brief Lex a string.
  /// \return The lexed string as a string.
  zc::StringPtr lexString();

  /// \brief Lex an escape sequence.
  /// \param flags The flags to use for scanning the escape sequence.
  /// \return The lexed escape sequence as a string.
  zc::StringPtr lexEscapeSequence(EscapeSequenceScanningFlags flags);

  /// \brief Lex a Unicode escape sequence at \u.
  /// \param shouldEmitInvalidEscapeError Whether to emit an error for an invalid escape sequence.
  /// \return The Unicode code point if a valid escape sequence is found, -1 otherwise.
  int32_t lexUnicodeEscape(bool shouldEmitInvalidEscapeError);

  /// \brief Lex an invalid character.
  void lexInvalidCharacter();

  /// \brief Report an error at the specified position.
  /// \tparam ID The diagnostic ID to report.
  /// \tparam ...Args The types of the arguments to format the diagnostic message.
  /// \param pos The position in the buffer where the error occurred.
  /// \param length The length of the text that caused the error.
  /// \param ...args The arguments to format the diagnostic message.
  template <diagnostics::DiagID ID, typename... Args>
  void errorAt(const zc::byte* pos, uint32_t length, Args&&... args);

  /// \brief Report an error.
  /// \tparam ID The diagnostic ID to report.
  /// \tparam ...Args The types of the arguments to format the diagnostic message.
  /// \param ...args The arguments to format the diagnostic message.
  template <diagnostics::DiagID ID, typename... Args>
  void error(Args&&... args);
};

Lexer::Impl::Impl(const source::SourceManager& sourceMgr,
                  diagnostics::DiagnosticEngine& diagnosticEngine,
                  const basic::LangOptions& options, basic::StringPool& stringPool,
                  const source::BufferId& bufferId)
    : sourceMgr(sourceMgr),
      diagnosticEngine(diagnosticEngine),
      langOpts(options),
      stringPool(stringPool),
      bufferId(bufferId) {
  // Initialize buffer pointers
  zc::ArrayPtr<const zc::byte> buffer = sourceMgr.getEntireTextForBuffer(bufferId);
  bufferStart = buffer.begin();
  bufferEnd = buffer.end();

  // Initialize embedded state
  state.curPtr = bufferStart;
  state.fullStartPtr = bufferStart;
  state.tokenStartPtr = bufferStart;
  state.tokenFlags = TokenFlags::None;
}

int32_t Lexer::Impl::ch() const {
  if (state.curPtr >= bufferEnd) { return -1; }
  return *state.curPtr;
}

int32_t Lexer::Impl::charAt(int32_t offset) const {
  if (state.curPtr + offset < bufferStart || state.curPtr + offset >= bufferEnd) { return -1; }
  return *(state.curPtr + offset);
}

std::pair<uint32_t, uint32_t> Lexer::Impl::charWithSize() const {
  return lexer::charWithSize(state.curPtr, bufferEnd);
}

void Lexer::Impl::formToken(ast::SyntaxKind kind, zc::Maybe<zc::StringPtr> value) {
  source::SourceLoc startLoc(state.tokenStartPtr);
  source::SourceLoc endLoc(state.curPtr);
  source::SourceRange range(startLoc, endLoc);

  // Create token
  state.token = Token(kind, range, value, state.tokenFlags);
}

void Lexer::Impl::processCommentDirective(const zc::byte* start, const zc::byte* end,
                                          bool isMultiLine) {
  const zc::byte* pos = start;

  if (isMultiLine) {
    // Skip whitespace
    while (pos < end && (*pos == ' ' || *pos == '\t')) { pos++; }
    // Skip combinations of / and *
    while (pos < end && (*pos == '/' || *pos == '*')) { pos++; }
  } else {
    // Skip opening //
    pos += 2;
    // Skip another / if present
    while (pos < end && *pos == '/') { pos++; }
  }

  // Skip whitespace
  while (pos < end && (*pos == ' ' || *pos == '\t')) { pos++; }

  // Directive must start with '@'
  if (!(pos < end && *pos == '@')) { return; }
  pos++;

  CommentDirectiveKind kind;
  zc::StringPtr text =
      stringPool.intern((zc::heapString(reinterpret_cast<const char*>(pos), end - pos)));

  if (text.startsWith("zom-expect-error")) {
    kind = CommentDirectiveKind::ExpectError;
  } else if (text.startsWith("zom-ignore")) {
    kind = CommentDirectiveKind::Ignore;
  } else {
    return;
  }

  CommentDirective directive = {source::SourceRange(start, end), kind};
  commentDirectives.add(zc::mv(directive));
}

void Lexer::Impl::getIdentifierToken(zc::StringPtr text) {
  ast::SyntaxKind kind = getKeywordKind(text.asBytes());
  formToken(kind, text);
}

zc::StringPtr Lexer::Impl::lexHexDigits(int32_t minCount, bool scanAsManyAsPossible,
                                        bool canHaveSeparators) {
  zc::Vector<char> valueChars;
  bool allowSeparator = false;
  bool isPreviousTokenSeparator = false;

  while (valueChars.size() < minCount || scanAsManyAsPossible) {
    int32_t c = ch();

    if (canHaveSeparators && c == '_') {
      state.tokenFlags |= TokenFlags::ContainsSeparator;
      if (allowSeparator) {
        allowSeparator = false;
        isPreviousTokenSeparator = true;
      } else if (isPreviousTokenSeparator) {
        errorAt<diagnostics::DiagID::MultipleConsecutiveNumericSeparatorsAreNotPermitted>(
            state.curPtr, 1);
      } else {
        errorAt<diagnostics::DiagID::NumericSeparatorsAreNotAllowedHere>(state.curPtr, 1);
      }
      state.curPtr++;
      continue;
    }

    allowSeparator = canHaveSeparators;
    if (c >= 'A' && c <= 'F') {
      c += 'a' - 'A';  // standardize hex literals to lowercase
    } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
      break;
    }

    valueChars.add(static_cast<char>(c));
    state.curPtr++;
    isPreviousTokenSeparator = false;
  }

  if (valueChars.size() < minCount) { return ""_zc; }

  if (charAt(-1) == '_') {
    errorAt<diagnostics::DiagID::NumericSeparatorsAreNotAllowedHere>(state.curPtr - 1, 1);
  }

  return stringPool.intern(valueChars.releaseAsArray());
}

zc::StringPtr Lexer::Impl::lexBinaryOrOctalDigits(int32_t base) {
  zc::Vector<char> valueChars;
  bool allowSeparator = false;
  bool isPreviousTokenSeparator = false;

  while (true) {
    int32_t c = ch();

    if (isDigit(c) && c - '0' < base) {
      valueChars.add(static_cast<char>(c));
      allowSeparator = true;
      isPreviousTokenSeparator = false;
    } else if (c == '_') {
      state.tokenFlags |= TokenFlags::ContainsSeparator;
      if (allowSeparator) {
        allowSeparator = false;
        isPreviousTokenSeparator = true;
      } else if (isPreviousTokenSeparator) {
        errorAt<diagnostics::DiagID::MultipleConsecutiveNumericSeparatorsAreNotPermitted>(
            state.curPtr, 1);
      } else {
        errorAt<diagnostics::DiagID::NumericSeparatorsAreNotAllowedHere>(state.curPtr, 1);
      }
    } else {
      break;
    }
    state.curPtr++;
  }

  if (isPreviousTokenSeparator) {
    errorAt<diagnostics::DiagID::NumericSeparatorsAreNotAllowedHere>(state.curPtr - 1, 1);
  }

  return stringPool.intern(valueChars.releaseAsArray());
}

void Lexer::Impl::lex() {
  state.fullStartPtr = state.curPtr;
  state.tokenFlags = TokenFlags::None;

  while (true) {
    state.tokenStartPtr = state.curPtr;
    int32_t c = ch();

    switch (c) {
      case '\r':
      case '\n':
        state.tokenFlags |= TokenFlags::PrecedingLineBreak;
        state.curPtr++;
        continue;
      case ' ':   // space
      case '\t':  // tab
      case '\v':  // vertical tab
      case '\f':  // form feed
        state.curPtr++;
        continue;
      case '!':
        if (charAt(1) == '=') {
          if (charAt(2) == '=') {
            state.curPtr += 3;
            return formToken(ast::SyntaxKind::ExclamationEqualsEquals);
          }
          state.curPtr += 2;
          return formToken(ast::SyntaxKind::ExclamationEquals);
        }
        if (charAt(1) == '!') {
          state.curPtr += 2;
          return formToken(ast::SyntaxKind::ErrorUnwrap);
        }
        state.curPtr++;
        return formToken(ast::SyntaxKind::Exclamation);
      case '"':
      case '\'': {
        zc::StringPtr str = lexString();
        return formToken(ast::SyntaxKind::StringLiteral, str);
      }
      case '`': {
        zc::StringPtr str;
        ast::SyntaxKind kind = lexTemplateAndRetTokenValue(false, str);
        formToken(kind, str);
        return;
      }
      case '%':
        if (charAt(1) == '=') {
          state.curPtr += 2;
          return formToken(ast::SyntaxKind::PercentEquals);
        }
        state.curPtr++;
        return formToken(ast::SyntaxKind::Percent);
      case '&':
        if (charAt(1) == '&') {
          if (charAt(2) == '=') {
            state.curPtr += 3;
            return formToken(ast::SyntaxKind::AmpersandAmpersandEquals);
          }
          state.curPtr += 2;
          return formToken(ast::SyntaxKind::AmpersandAmpersand);
        }
        if (charAt(1) == '=') {
          state.curPtr += 2;
          return formToken(ast::SyntaxKind::AmpersandEquals);
        }
        state.curPtr++;
        return formToken(ast::SyntaxKind::Ampersand);
      case '(':
        state.curPtr++;
        return formToken(ast::SyntaxKind::LeftParen);
      case ')':
        state.curPtr++;
        return formToken(ast::SyntaxKind::RightParen);
      case '*':
        if (charAt(1) == '=') {
          state.curPtr += 2;
          return formToken(ast::SyntaxKind::AsteriskEquals);
        }
        if (charAt(1) == '*') {
          if (charAt(2) == '=') {
            state.curPtr += 3;
            return formToken(ast::SyntaxKind::AsteriskAsteriskEquals);
          }
          state.curPtr += 2;
          return formToken(ast::SyntaxKind::AsteriskAsterisk);
        }
        state.curPtr++;
        return formToken(ast::SyntaxKind::Asterisk);
      case '+':
        if (charAt(1) == '+') {
          state.curPtr += 2;
          return formToken(ast::SyntaxKind::PlusPlus);
        }
        if (charAt(1) == '=') {
          state.curPtr += 2;
          return formToken(ast::SyntaxKind::PlusEquals);
        }
        state.curPtr++;
        return formToken(ast::SyntaxKind::Plus);
      case ',':
        state.curPtr++;
        return formToken(ast::SyntaxKind::Comma);
      case '-':
        if (charAt(1) == '-') {
          state.curPtr += 2;
          return formToken(ast::SyntaxKind::MinusMinus);
        }
        if (charAt(1) == '=') {
          state.curPtr += 2;
          return formToken(ast::SyntaxKind::MinusEquals);
        }
        if (charAt(1) == '>') {
          state.curPtr += 2;
          return formToken(ast::SyntaxKind::Arrow);
        }
        state.curPtr++;
        return formToken(ast::SyntaxKind::Minus);
      case '.':
        if (isdigit(charAt(1))) { return lexNumber(); }
        if (charAt(1) == '.' && charAt(2) == '.') {
          state.curPtr += 3;
          return formToken(ast::SyntaxKind::DotDotDot);
        }
        state.curPtr++;
        return formToken(ast::SyntaxKind::Period);
      case '/':
        // Single-line comment
        if (charAt(1) == '/') {
          state.curPtr += 2;

          while (true) {
            auto [code, size] = charWithSize();
            if (size == 0 || isLineBreak(code)) { break; }
            state.curPtr += size;
          }

          processCommentDirective(state.tokenStartPtr, state.curPtr, false);
          continue;
        }

        // Multi-line comment
        if (charAt(1) == '*') {
          state.curPtr += 2;

          bool commentClosed = false;
          const zc::byte* lastLineStartPtr = state.tokenStartPtr;

          while (true) {
            auto [code, size] = charWithSize();
            if (size == 0) { break; }

            if (code == '*' && charAt(1) == '/') {
              state.curPtr += 2;
              commentClosed = true;
              break;
            }

            state.curPtr += size;

            if (isLineBreak(code)) {
              lastLineStartPtr = state.curPtr;
              state.tokenFlags |= TokenFlags::PrecedingLineBreak;
            }
          }

          processCommentDirective(lastLineStartPtr, state.curPtr, true);

          if (!commentClosed) { error<diagnostics::DiagID::AsteriskSlashExpected>(); }

          continue;
        }

        if (charAt(1) == '=') {
          state.curPtr += 2;
          return formToken(ast::SyntaxKind::SlashEquals);
        }

        state.curPtr++;
        return formToken(ast::SyntaxKind::Slash);
      case '0':
        if (charAt(1) == 'X' || charAt(1) == 'x') {
          const zc::byte* start = state.curPtr;
          state.curPtr += 2;
          zc::StringPtr digits = lexHexDigits(1, true, true);
          if (digits == ""_zc) {
            error<diagnostics::DiagID::HexadecimalDigitExpected>();
            digits = "0"_zc;
          }
          zc::StringPtr rawText = stringPool.intern(zc::arrayPtr(start, state.curPtr).asChars());
          zc::StringPtr tokenValue;
          if (rawText.startsWith("0x") && rawText.slice(2) == digits) {
            tokenValue = digits;
          } else {
            tokenValue = stringPool.intern("0x", digits);
          }
          state.tokenFlags |= TokenFlags::HexSpecifier;
          return formToken(ast::SyntaxKind::IntegerLiteral, tokenValue);
        }
        if (charAt(1) == 'B' || charAt(1) == 'b') {
          state.curPtr += 2;
          zc::StringPtr digits = lexBinaryOrOctalDigits(2);
          if (digits == ""_zc) {
            error<diagnostics::DiagID::BinaryOrOctalDigitExpected>();
            digits = "0"_zc;
          }
          zc::StringPtr tokenValue = stringPool.intern("0b", digits);
          state.tokenFlags |= TokenFlags::BinarySpecifier;
          return formToken(ast::SyntaxKind::IntegerLiteral, tokenValue);
        }
        if (charAt(1) == 'O' || charAt(1) == 'o') {
          state.curPtr += 2;
          zc::StringPtr digits = lexBinaryOrOctalDigits(8);
          if (digits == ""_zc) {
            error<diagnostics::DiagID::OctalDigitExpected>();
            digits = "0"_zc;
          }
          zc::StringPtr tokenValue = stringPool.intern("0o", digits);
          state.tokenFlags |= TokenFlags::OctalSpecifier;
          return formToken(ast::SyntaxKind::IntegerLiteral, tokenValue);
        }
        ZC_FALLTHROUGH;
      case '1' ... '9':
        return lexNumber();
      case ':':
        state.curPtr++;
        return formToken(ast::SyntaxKind::Colon);
      case ';':
        state.curPtr++;
        return formToken(ast::SyntaxKind::Semicolon);
      case '<':
        if (charAt(1) == '<') {
          if (charAt(2) == '=') {
            state.curPtr += 3;
            return formToken(ast::SyntaxKind::LessThanLessThanEquals);
          }
          state.curPtr += 2;
          return formToken(ast::SyntaxKind::LessThanLessThan);
        }
        if (charAt(1) == '=') {
          state.curPtr += 2;
          return formToken(ast::SyntaxKind::LessThanEquals);
        }
        state.curPtr++;
        return formToken(ast::SyntaxKind::LessThan);
      case '=':
        if (charAt(1) == '=') {
          if (charAt(2) == '=') {
            state.curPtr += 3;
            return formToken(ast::SyntaxKind::EqualsEqualsEquals);
          }
          state.curPtr += 2;
          return formToken(ast::SyntaxKind::EqualsEquals);
        }
        if (charAt(1) == '>') {
          state.curPtr += 2;
          return formToken(ast::SyntaxKind::EqualsGreaterThan);
        }
        state.curPtr++;
        return formToken(ast::SyntaxKind::Equals);
      case '>':
        if (charAt(1) == '>') {
          if (charAt(2) == '=') {
            state.curPtr += 3;
            return formToken(ast::SyntaxKind::GreaterThanGreaterThanEquals);
          }
          state.curPtr += 2;
          return formToken(ast::SyntaxKind::GreaterThanGreaterThan);
        }
        if (charAt(1) == '=') {
          state.curPtr += 2;
          return formToken(ast::SyntaxKind::GreaterThanEquals);
        }
        state.curPtr++;
        return formToken(ast::SyntaxKind::GreaterThan);
      case '?':
        if (charAt(1) == '.' && isdigit(charAt(2))) {
          state.curPtr += 2;
          return formToken(ast::SyntaxKind::QuestionDot);
        }
        if (charAt(1) == '?') {
          if (charAt(2) == '=') {
            state.curPtr += 3;
            return formToken(ast::SyntaxKind::QuestionQuestionEquals);
          }
          state.curPtr += 2;
          return formToken(ast::SyntaxKind::QuestionQuestion);
        }
        state.curPtr++;
        return formToken(ast::SyntaxKind::Question);
      case '[':
        state.curPtr++;
        return formToken(ast::SyntaxKind::LeftBracket);
      case ']':
        state.curPtr++;
        return formToken(ast::SyntaxKind::RightBracket);
      case '^':
        if (charAt(1) == '=') {
          state.curPtr += 2;
          return formToken(ast::SyntaxKind::CaretEquals);
        }
        state.curPtr++;
        return formToken(ast::SyntaxKind::Caret);
      case '{':
        state.curPtr++;
        return formToken(ast::SyntaxKind::LeftBrace);
      case '|':
        if (charAt(1) == '|') {
          if (charAt(2) == '=') {
            state.curPtr += 3;
            return formToken(ast::SyntaxKind::BarBarEquals);
          }
          state.curPtr += 2;
          return formToken(ast::SyntaxKind::BarBar);
        }
        if (charAt(1) == '=') {
          state.curPtr += 2;
          return formToken(ast::SyntaxKind::BarEquals);
        }
        state.curPtr++;
        return formToken(ast::SyntaxKind::Bar);
      case '}':
        state.curPtr++;
        return formToken(ast::SyntaxKind::RightBrace);
      case '~':
        state.curPtr++;
        return formToken(ast::SyntaxKind::Tilde);
      case '@':
        state.curPtr++;
        return formToken(ast::SyntaxKind::At);
      case '\\': {
        const int32_t cp = peekUnicodeEscape();

        if (cp >= 0 && isIdentifierStart(cp)) {
          zc::StringPtr tokenValue =
              stringPool.intern(encodeUtf8(lexUnicodeEscape(true)), lexIdentifierParts());
          return getIdentifierToken(tokenValue);
        }

        return lexInvalidCharacter();
      }
      case '#':
        if (charAt(1) == '!') {
          if (state.curPtr == bufferStart) {
            state.curPtr += 2;
            while (true) {
              auto [code, size] = charWithSize();
              if (size == 0 || isLineBreak(code)) { break; }
              state.curPtr += size;
            }
            continue;
          }
          errorAt<diagnostics::DiagID::XCanOnlyUsedAtStartOfFile>(state.curPtr, 2);
          state.curPtr++;
          return formToken(ast::SyntaxKind::Unknown);
        }
        state.curPtr++;
        return formToken(ast::SyntaxKind::Hash);
      default:
        if (c < 0) { return formToken(ast::SyntaxKind::EndOfFile); }
        ZC_IF_SOME(tokenValue, lexIdentifier(0)) { return getIdentifierToken(tokenValue); }
        auto [code, size] = charWithSize();
        if (code == kInvalidCodePoint && size == 1) {
          errorAt<diagnostics::DiagID::FileAppearsToBeBinary>(bufferStart, 0);
          state.curPtr = bufferEnd;
          return formToken(ast::SyntaxKind::NonTextFileMarker);
        }
        if (isWhiteSpaceSingleLine(code)) {
          state.curPtr += size;
          continue;
        }
        if (isLineBreak(code)) {
          state.tokenFlags |= TokenFlags::PrecedingLineBreak;
          state.curPtr += size;
          continue;
        }
        return lexInvalidCharacter();
    }
  }
}

zc::Maybe<zc::StringPtr> Lexer::Impl::lexIdentifier(int32_t prefixLength) {
  const zc::byte* start = state.curPtr;
  state.curPtr += prefixLength;
  int32_t ch = this->ch();

  // Fast path for simple ASCII identifiers
  if (isASCIILetter(ch) || ch == '_' || ch == '$') {
    while (true) {
      state.curPtr++;
      ch = this->ch();
      if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
            ch == '_' || ch == '$')) {
        break;
      }
    }
    if (ch < 0x80 && ch != '\\') {
      return stringPool.intern(
          zc::heapString(reinterpret_cast<const char*>(start), state.curPtr - start));
    }
    state.curPtr = start + prefixLength;
  }

  auto [decodedChar, size] = charWithSize();
  ch = decodedChar;

  if (isIdentifierStart(ch)) {
    while (true) {
      state.curPtr += size;
      auto [nextChar, nextSize] = charWithSize();
      ch = nextChar;
      size = nextSize;
      if (!isIdentifierPart(ch)) { break; }
    }

    zc::StringPtr tokenValue = stringPool.intern(
        zc::heapString(reinterpret_cast<const char*>(start), state.curPtr - start));
    if (ch == '\\') { tokenValue = stringPool.intern(tokenValue, lexIdentifierParts()); }
    return tokenValue;
  }

  return zc::none;
}

int32_t Lexer::Impl::peekUnicodeEscape() {
  if (charAt(1) == 'u') {
    const zc::byte* savePtr = state.curPtr;
    const TokenFlags saveTokenFlags = state.tokenFlags;
    const int32_t codePoint = lexUnicodeEscape(false);
    state.curPtr = savePtr;
    state.tokenFlags = saveTokenFlags;
    return codePoint;
  }
  return -1;
}

void Lexer::Impl::lexNumber() {
  const zc::byte* start = state.curPtr;
  zc::StringPtr fixedPart;

  if (ch() == '0') {
    state.curPtr++;
    if (ch() == '_') {
      state.tokenFlags |= TokenFlags::ContainsSeparator | TokenFlags::ContainsInvalidSeparator;
      errorAt<diagnostics::DiagID::NumericSeparatorsAreNotAllowedHere>(state.curPtr, 1);
      state.curPtr = start;
      fixedPart = lexNumberFragment();
    } else {
      auto [digits, isOctal] = lexDigits();
      if (digits == ""_zc) {
        // Single zero
        fixedPart = "0"_zc;
      } else if (!isOctal) {
        // NonOctalDecimalIntegerLiteral - leading zero with non-octal digits
        state.tokenFlags |= TokenFlags::ContainsLeadingZero;
        fixedPart = digits;
      } else {
        // Octal literal (legacy)
        state.tokenFlags |= TokenFlags::Octal;

        zc::StringPtr octalDigits = digits;
        // Use standard strtoll for octal parsing since zc::String::parseAs doesn't support radix
        // argument
        int64_t octalValue = strtoll(octalDigits.cStr(), nullptr, 8);
        zc::StringPtr decimalValue = stringPool.intern(octalValue);

        // Check if we have a preceding minus token for proper diagnostic range
        const bool withMinus = state.token.is(ast::SyntaxKind::Minus);
        zc::StringPtr suggestion = stringPool.intern(withMinus ? "-0o" : "0o", octalDigits);
        const zc::byte* errorStart = withMinus ? start - 1 : start;
        const size_t errorLength = withMinus ? state.curPtr - start + 1 : state.curPtr - start;

        // Report error: Octal literals are not allowed. Use the syntax '0o...'
        errorAt<diagnostics::DiagID::OctalLiteralsAreNotAllowedUseTheSyntax0o>(
            errorStart, errorLength, suggestion);

        // Fallback: Treat as decimal integer literal for recovery
        // We'll use the parsed octal value as the token value so compilation can proceed with
        // intended value
        return formToken(ast::SyntaxKind::IntegerLiteral, decimalValue);
      }
    }
  } else {
    fixedPart = lexNumberFragment();
  }

  const zc::byte* fixedPartEnd = state.curPtr;
  zc::StringPtr fractionalPart = ""_zc;
  zc::StringPtr exponentPart = ""_zc;
  zc::StringPtr exponentPreamble = ""_zc;

  if (ch() == '.') {
    state.curPtr++;
    fractionalPart = lexNumberFragment();
  }

  const zc::byte* end = state.curPtr;
  if (ch() == 'E' || ch() == 'e') {
    state.curPtr++;
    state.tokenFlags |= TokenFlags::Scientific;
    if (ch() == '+' || ch() == '-') { state.curPtr++; }
    const zc::byte* startNumericPart = state.curPtr;
    exponentPart = lexNumberFragment();
    if (exponentPart == ""_zc) {
      error<diagnostics::DiagID::DigitExpected>();
    } else {
      exponentPreamble = stringPool.intern(zc::arrayPtr(end, startNumericPart).asChars());
      end = state.curPtr;
    }
  }

  zc::StringPtr tokenValue;
  if (hasFlag(state.tokenFlags, TokenFlags::ContainsSeparator)) {
    tokenValue = fixedPart;
    if (fractionalPart != ""_zc) {
      tokenValue = stringPool.intern(tokenValue, ".", fractionalPart);
    }
    if (exponentPart != ""_zc) {
      tokenValue = stringPool.intern(tokenValue, exponentPreamble, exponentPart);
    }
  } else {
    tokenValue = stringPool.intern(zc::arrayPtr(start, end).asChars());
  }

  if (hasFlag(state.tokenFlags, TokenFlags::ContainsLeadingZero)) {
    errorAt<diagnostics::DiagID::DecimalsWithLeadingZerosAreNotAllowed>(start,
                                                                        state.curPtr - start);
    // Normalize the value by parsing as number and converting back to string
    // This removes leading zeros while preserving the numeric value
    if (fractionalPart != ""_zc || hasFlag(state.tokenFlags, TokenFlags::Scientific)) {
      // For floating point numbers, parse as double
      double floatValue = tokenValue.parseAs<double>();
      tokenValue = stringPool.intern(floatValue);
    } else {
      // For integers, parse as int64_t
      int64_t intValue = tokenValue.parseAs<int64_t>();
      tokenValue = stringPool.intern(intValue);
    }
  }

  ast::SyntaxKind result;
  if (fixedPartEnd == state.curPtr) {
    result = lexBigIntSuffix(tokenValue);
  } else {
    // Additional characters consumed (fractional part or scientific notation)
    // Normalize the token value and set appropriate result type
    if (fractionalPart != ""_zc || hasFlag(state.tokenFlags, TokenFlags::Scientific)) {
      result = ast::SyntaxKind::FloatLiteral;
      tokenValue = stringPool.intern(tokenValue.parseAs<double>());
    } else {
      result = ast::SyntaxKind::IntegerLiteral;
      tokenValue = stringPool.intern(tokenValue.parseAs<int64_t>());
    }
  }

  // Identifier start check after number
  auto [code, size] = charWithSize();
  if (isIdentifierStart(code)) {
    const zc::byte* idStart = state.curPtr;
    zc::StringPtr id = lexIdentifierParts();

    // Check if it was a BigInt 'n' that we missed earlier
    if (result != ast::SyntaxKind::BigIntLiteral && id.size() == 1 && id == "n"_zc) {
      if (hasFlag(state.tokenFlags, TokenFlags::Scientific)) {
        errorAt<diagnostics::DiagID::ABigIntLiteralCannotUseExponentialNotation>(
            start, state.curPtr - start);
        return formToken(result, tokenValue);
      }
      if (fixedPartEnd < idStart) {
        errorAt<diagnostics::DiagID::ABigIntLiteralMustBeAnInteger>(start, state.curPtr - start);
        return formToken(result, tokenValue);
      }
    }

    errorAt<diagnostics::DiagID::AnIdentifierOrKeywordCannotImmediatelyFollowANumericLiteral>(
        idStart, state.curPtr - idStart);
    state.curPtr = idStart;
  }

  return formToken(result, tokenValue);
}

zc::StringPtr Lexer::Impl::lexNumberFragment() {
  const zc::byte* start = state.curPtr;
  bool allowSeparator = false;
  bool isPreviousTokenSeparator = false;
  zc::Vector<char> result;

  while (true) {
    int32_t c = ch();
    if (c == '_') {
      state.tokenFlags |= TokenFlags::ContainsSeparator;
      if (allowSeparator) {
        allowSeparator = false;
        isPreviousTokenSeparator = true;
        result.addAll(zc::arrayPtr(start, state.curPtr).asChars());
      } else {
        state.tokenFlags |= TokenFlags::ContainsInvalidSeparator;
        if (isPreviousTokenSeparator) {
          errorAt<diagnostics::DiagID::MultipleConsecutiveNumericSeparatorsAreNotPermitted>(
              state.curPtr, 1);
        } else {
          errorAt<diagnostics::DiagID::NumericSeparatorsAreNotAllowedHere>(state.curPtr, 1);
        }
      }
      state.curPtr++;
      start = state.curPtr;
      continue;
    }
    if (isDigit(c)) {
      allowSeparator = true;
      isPreviousTokenSeparator = false;
      state.curPtr++;
      continue;
    }
    break;
  }

  if (isPreviousTokenSeparator) {
    state.tokenFlags |= TokenFlags::ContainsInvalidSeparator;
    errorAt<diagnostics::DiagID::NumericSeparatorsAreNotAllowedHere>(state.curPtr - 1, 1);
  }

  result.addAll(zc::arrayPtr(start, state.curPtr).asChars());
  return stringPool.intern(result.releaseAsArray());
}

std::pair<zc::StringPtr, bool> Lexer::Impl::lexDigits() {
  const zc::byte* start = state.curPtr;
  bool isOctal = true;
  while (isDigit(ch())) {
    if (!isOctalDigit(static_cast<zc::byte>(ch()))) { isOctal = false; }
    state.curPtr++;
  }
  if (state.curPtr == start) { return {""_zc, isOctal}; }
  return {stringPool.intern(zc::arrayPtr(start, state.curPtr).asChars()), isOctal};
}

ast::SyntaxKind Lexer::Impl::lexBigIntSuffix(zc::StringPtr& tokenValue) {
  if (ch() == 'n') {
    tokenValue = stringPool.intern(tokenValue, "n");
    if (hasFlag(state.tokenFlags, TokenFlags::BinaryOrOctalSpecifier)) {
      // Use the utility function to convert binary/octal string to its base-10 representation.
      zc::String base10Value = parsePseudoBigInt(tokenValue);
      tokenValue = stringPool.intern(base10Value, "n");
    }
    state.curPtr++;
    return ast::SyntaxKind::BigIntLiteral;
  }

  // Parse integer value to normalize it (e.g. remove leading zeros)
  // Only if not already processed above (BinaryOrOctalSpecifier)
  int64_t intValue = hasFlag(state.tokenFlags, TokenFlags::BinarySpecifier)
                         ? strtoll(tokenValue.slice(2).cStr(), nullptr, 2)
                     : hasFlag(state.tokenFlags, TokenFlags::OctalSpecifier)
                         ? strtoll(tokenValue.slice(2).cStr(), nullptr, 8)
                         : tokenValue.parseAs<int64_t>();

  tokenValue = stringPool.intern(intValue);
  return ast::SyntaxKind::IntegerLiteral;
}

ast::SyntaxKind Lexer::Impl::lexTemplateAndRetTokenValue(bool shouldEmitInvalidEscapeError,
                                                         zc::StringPtr& outValue) {
  const bool startedWithBacktick = ch() == '`';

  state.curPtr++;
  const zc::byte* start = state.curPtr;

  zc::Vector<char> result;
  ast::SyntaxKind tokenKind = ast::SyntaxKind::Unknown;

  while (true) {
    if (state.curPtr >= bufferEnd) {
      result.addAll(zc::arrayPtr(start, state.curPtr).asChars());
      state.tokenFlags |= TokenFlags::Unterminated;
      error<diagnostics::DiagID::UnterminatedTemplateLiteral>();
      tokenKind = startedWithBacktick ? ast::SyntaxKind::NoSubstitutionTemplateLiteral
                                      : ast::SyntaxKind::TemplateTail;
      break;
    }

    const zc::byte c = ch();
    if (c == '`') {
      result.addAll(zc::arrayPtr(start, state.curPtr).asChars());
      state.curPtr++;
      tokenKind = startedWithBacktick ? ast::SyntaxKind::NoSubstitutionTemplateLiteral
                                      : ast::SyntaxKind::TemplateTail;
      break;
    }

    if (c == '$' && state.curPtr + 1 < bufferEnd && *(state.curPtr + 1) == '{') {
      result.addAll(zc::arrayPtr(start, state.curPtr).asChars());
      state.curPtr += 2;
      tokenKind =
          startedWithBacktick ? ast::SyntaxKind::TemplateHead : ast::SyntaxKind::TemplateMiddle;
      break;
    }

    if (c == '\\') {
      result.addAll(zc::arrayPtr(start, state.curPtr).asChars());
      EscapeSequenceScanningFlags flags = EscapeSequenceScanningFlags::String;
      if (shouldEmitInvalidEscapeError) { flags |= EscapeSequenceScanningFlags::ReportErrors; }
      zc::StringPtr esc = lexEscapeSequence(flags);
      result.addAll(esc.asArray());
      start = state.curPtr;
      continue;
    }

    if (c == '\r') {
      result.addAll(zc::arrayPtr(start, state.curPtr).asChars());
      state.curPtr++;
      if (state.curPtr < bufferEnd && ch() == '\n') { state.curPtr++; }
      result.add('\n');
      start = state.curPtr;
      continue;
    }

    state.curPtr++;
  }

  ZC_DASSERT(tokenKind != ast::SyntaxKind::Unknown);

  outValue = stringPool.intern(result.releaseAsArray());
  return tokenKind;
}

zc::StringPtr Lexer::Impl::lexIdentifierParts() {
  zc::Vector<char> result;
  const zc::byte* start = state.curPtr;

  while (true) {
    auto [code, size] = charWithSize();
    if (isIdentifierPart(code)) {
      state.curPtr += size;
      continue;
    }

    if (ch() == '\\') {
      const int32_t escaped = peekUnicodeEscape();
      if (escaped >= 0 && isIdentifierPart(escaped)) {
        result.addAll(zc::arrayPtr(start, state.curPtr).asChars());
        zc::String utf8 = encodeUtf8(lexUnicodeEscape(true));
        result.addAll(utf8.asArray());
        start = state.curPtr;
        continue;
      }
    }
    break;
  }
  result.addAll(zc::arrayPtr(start, state.curPtr).asChars());
  return stringPool.intern(result.releaseAsArray());
}

zc::StringPtr Lexer::Impl::lexString() {
  zc::byte quoteChar = ch();
  state.curPtr++;
  zc::Vector<char> result;
  const zc::byte* start = state.curPtr;

  while (true) {
    if (state.curPtr >= bufferEnd) {
      result.addAll(zc::arrayPtr(start, state.curPtr).asChars());
      state.tokenFlags |= TokenFlags::Unterminated;
      error<diagnostics::DiagID::UnterminatedString>();
      break;
    }
    const zc::byte c = ch();
    if (c == quoteChar) {
      result.addAll(zc::arrayPtr(start, state.curPtr).asChars());
      state.curPtr++;
      break;
    }
    if (c == '\\') {
      result.addAll(zc::arrayPtr(start, state.curPtr).asChars());
      zc::StringPtr esc = lexEscapeSequence(EscapeSequenceScanningFlags::String |
                                            EscapeSequenceScanningFlags::ReportErrors);
      result.addAll(esc.asArray());
      start = state.curPtr;
      continue;
    }
    if ((c == '\n' || c == '\r')) {
      result.addAll(zc::arrayPtr(start, state.curPtr).asChars());
      state.tokenFlags |= TokenFlags::Unterminated;
      error<diagnostics::DiagID::UnterminatedString>();
      break;
    }
    state.curPtr++;
  }
  return stringPool.intern(result.releaseAsArray());
}

zc::StringPtr Lexer::Impl::lexEscapeSequence(EscapeSequenceScanningFlags flags) {
  const zc::byte* start = state.curPtr;
  state.curPtr++;
  if (state.curPtr >= bufferEnd) {
    error<diagnostics::DiagID::UnexpectedEndOfText>();
    return ""_zc;
  }

  zc::byte c = ch();
  state.curPtr++;
  switch (c) {
    case '0':
      if (state.curPtr >= bufferEnd || !isdigit(ch())) { return "\0"_zc; }
      ZC_FALLTHROUGH;
    case '1':
    case '2':
    case '3':
      if (state.curPtr < bufferEnd && isOctalDigit(ch())) { state.curPtr++; }
      ZC_FALLTHROUGH;
    case '4':
    case '5':
    case '6':
    case '7': {
      if (state.curPtr < bufferEnd && isOctalDigit(ch())) { state.curPtr++; }
      state.tokenFlags |= TokenFlags::ContainsInvalidEscape;
      if (hasFlag(flags, EscapeSequenceScanningFlags::ReportInvalidEscapeErrors)) {
        // Parse octal value
        uint32_t code = parseOctalValue(zc::arrayPtr(start + 1, state.curPtr));
        zc::String hexValue = formatHexValue(code);

        if (hasFlag(flags, EscapeSequenceScanningFlags::RegularExpression) &&
            !hasFlag(flags, EscapeSequenceScanningFlags::AtomEscape) && c != '0') {
          error<diagnostics::DiagID::OctalEscapeSequencesAndBackreferencesNotAllowed>(
              zc::mv(hexValue));
        } else {
          error<diagnostics::DiagID::OctalEscapeSequencesNotAllowed>(zc::mv(hexValue));
        }
        return stringPool.intern(encodeUtf8(code));
      }
      return stringPool.intern(zc::arrayPtr(start, state.curPtr).asChars());
    }
    case '8':
    case '9': {
      state.tokenFlags |= TokenFlags::ContainsInvalidEscape;
      if (hasFlag(flags, EscapeSequenceScanningFlags::ReportInvalidEscapeErrors)) {
        if (hasFlag(flags, EscapeSequenceScanningFlags::RegularExpression) &&
            !hasFlag(flags, EscapeSequenceScanningFlags::AtomEscape)) {
          error<diagnostics::DiagID::DecimalEscapeSequencesAndBackreferencesNotAllowed>(
              formatHexValue(c - '0'));
        } else {
          error<diagnostics::DiagID::EscapeSequenceNotAllowed>(
              zc::heapString(zc::arrayPtr(start, state.curPtr).asChars()));
        }
        return stringPool.intern(encodeUtf8(c));
      }
      return stringPool.intern(zc::arrayPtr(start, state.curPtr).asChars());
    }
    case 'b':
      return stringPool.intern('\b');
    case 't':
      return stringPool.intern('\t');
    case 'n':
      return stringPool.intern('\n');
    case 'v':
      return stringPool.intern('\v');
    case 'f':
      return stringPool.intern('\f');
    case 'r':
      return stringPool.intern('\r');
    case '\'':
      return stringPool.intern('\'');
    case '"':
      return stringPool.intern('"');
    case 'u': {
      // '\uDDDD' and '\U{DDDDDD}'
      state.curPtr -= 2;
      uint32_t codePoint =
          lexUnicodeEscape(hasFlag(flags, EscapeSequenceScanningFlags::ReportInvalidEscapeErrors));
      if (codePoint < 0) { return stringPool.intern(zc::arrayPtr(start, state.curPtr).asChars()); }
      return stringPool.intern(encodeUtf8(codePoint));
    }
    case 'x': {
      for (; state.curPtr < start + 4; state.curPtr++) {
        if (!(state.curPtr < bufferEnd && isxdigit(*state.curPtr))) {
          state.tokenFlags |= TokenFlags::ContainsInvalidEscape;
          if (hasFlag(flags, EscapeSequenceScanningFlags::ReportInvalidEscapeErrors)) {
            error<diagnostics::DiagID::InvalidCharacter>();
          }
          return stringPool.intern(zc::arrayPtr(start, state.curPtr).asChars());
        }
      }
      state.tokenFlags |= TokenFlags::HexEscape;
      int code = 0;
      zc::byte h1 = *(start + 2);
      zc::byte h2 = *(start + 3);
      auto hexVal = [](zc::byte h) -> int {
        if (h >= '0' && h <= '9') return h - '0';
        if (h >= 'A' && h <= 'F') return h - 'A' + 10;
        return h - 'a' + 10;
      };
      code = (hexVal(h1) << 4) | hexVal(h2);
      return stringPool.intern(static_cast<char>(code));
    }
    case '\r':
      if (state.curPtr < bufferEnd && ch() == '\n') { state.curPtr++; }
      ZC_FALLTHROUGH;
    case '\n':
      return ""_zc;
    default:
      if (hasFlag(flags, EscapeSequenceScanningFlags::AnyUnicodeMode) ||
          hasFlag(flags, EscapeSequenceScanningFlags::RegularExpression) ||
          (hasFlag(flags, EscapeSequenceScanningFlags::AnnexB) && isIdentifierPart(c))) {
        error<diagnostics::DiagID::ThisCharCannotBeEscapedInARegularExpression>();
      }
      return stringPool.intern(encodeUtf8(c));
  }
}

int32_t Lexer::Impl::lexUnicodeEscape(bool shouldEmitInvalidEscapeError) {
  state.curPtr += 2;
  const zc::byte* start = state.curPtr;
  bool extended = ch() == '{';
  zc::StringPtr hexDigits;

  if (extended) {
    state.curPtr++;
    state.tokenFlags |= TokenFlags::ExtendedUnicodeEscape;
    hexDigits = lexHexDigits(1, true, false);
  } else {
    state.tokenFlags |= TokenFlags::UnicodeEscape;
    hexDigits = lexHexDigits(4, false, false);
  }

  if (hexDigits == ""_zc) {
    state.tokenFlags |= TokenFlags::ContainsInvalidEscape;
    if (shouldEmitInvalidEscapeError) {
      error<diagnostics::DiagID::HexadecimalDigitExpected>();
      return -1;
    }
  }

  uint32_t hexValue = parseHexValue(hexDigits);
  if (extended) {
    if (hexValue > 0x10FFFF) {
      state.tokenFlags |= TokenFlags::ContainsInvalidEscape;
      if (shouldEmitInvalidEscapeError) {
        errorAt<diagnostics::DiagID::ExtendedUnicodeEscapeValueOutOfRange>(
            start + 1, state.curPtr - start - 1);
      }
      return -1;
    }

    if (ch() == '}') {
      state.tokenFlags |= TokenFlags::ContainsInvalidEscape;
      if (shouldEmitInvalidEscapeError) {
        error<diagnostics::DiagID::UnterminatedUnicodeEscapeSequence>();
      }
      return -1;
    }
    state.curPtr++;
  }

  return hexValue;
}

void Lexer::Impl::lexInvalidCharacter() {
  auto [code, size] = charWithSize();
  errorAt<diagnostics::DiagID::InvalidCharacter>(state.curPtr, size);
  state.curPtr += size;
  return formToken(ast::SyntaxKind::Unknown);
}

template <diagnostics::DiagID ID, typename... Args>
void Lexer::Impl::errorAt(const zc::byte* pos, uint32_t length, Args&&... args) {
  source::SourceLoc loc(pos);
  source::CharSourceRange range(loc, length);
  diagnosticEngine.diagnose<ID>(loc, zc::fwd<Args>(args)...).addRange(range);
}

template <diagnostics::DiagID ID, typename... Args>
void Lexer::Impl::error(Args&&... args) {
  source::SourceLoc loc = sourceMgr.getLocForOffset(bufferId, state.tokenStartPtr - bufferStart);
  diagnosticEngine.diagnose<ID>(loc, zc::fwd<Args>(args)...);
}

Lexer::Lexer(const source::SourceManager& sourceMgr,
             diagnostics::DiagnosticEngine& diagnosticEngine, const basic::LangOptions& options,
             basic::StringPool& stringPool, const source::BufferId& bufferId)
    : impl(zc::heap<Impl>(sourceMgr, diagnosticEngine, options, stringPool, bufferId)) {}

Lexer::~Lexer() = default;

void Lexer::lex(Token& outToken) {
  impl->lex();
  outToken = impl->state.token;
}

void Lexer::restoreState(LexerState s, bool enableDiagnostics) {
  impl->state.curPtr = s.curPtr;
  impl->state.fullStartPtr = s.fullStartPtr;
  impl->state.tokenStartPtr = s.tokenStartPtr;
  impl->state.token = s.token;
  impl->state.tokenFlags = s.tokenFlags;
}

const LexerState Lexer::getCurrentState() const { return impl->state; }

const source::SourceLoc Lexer::getFullStartLoc() const {
  return source::SourceLoc(impl->state.fullStartPtr);
}

const zc::Vector<CommentDirective>& Lexer::getCommentDirectives() const {
  return impl->commentDirectives;
}

}  // namespace lexer
}  // namespace compiler
}  // namespace zomlang
