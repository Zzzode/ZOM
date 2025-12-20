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

#pragma once

#include <cctype>
#include <cstdint>
#include <utility>

#include "zc/core/string.h"
#include "zomlang/compiler/ast/kinds.h"

namespace zomlang {
namespace compiler {
namespace lexer {

constexpr uint32_t kInvalidCodePoint = 0xFFFD;

// =======================================================================================
// Character Classification Utilities

/// \brief Check if a character can start an identifier.
/// \param c The character code to check.
/// \return True if the character is a valid identifier start, false otherwise.
bool isIdentifierStart(uint32_t c);

/// \brief Check if a character can be part of an identifier.
/// \param c The character code to check.
/// \return True if the character is a valid identifier part, false otherwise.
bool isIdentifierPart(uint32_t c);

/// \brief Check if a character is a Unicode identifier start.
/// \param codePoint The Unicode code point to check.
/// \return True if the code point is a Unicode identifier start, false otherwise.
bool isUnicodeIdentifierStart(const uint32_t codePoint);

/// \brief Check if a character is a Unicode identifier part.
/// \param codePoint The Unicode code point to check.
/// \return True if the code point is a Unicode identifier part, false otherwise.
bool isUnicodeIdentifierPart(const uint32_t codePoint);

/// \brief Check if a character is a word character (alphanumeric or underscore).
/// \param c The character to check.
/// \return True if the character is a word character, false otherwise.
bool isWordCharacter(uint32_t c);

/// \brief Check if a character is an ASCII letter.
/// \param ch The character to check.
/// \return True if the character is an ASCII letter, false otherwise.
bool isASCIILetter(int32_t ch);

/// \brief Check if a character is a decimal digit.
/// \param ch The character to check.
/// \return True if the character is a decimal digit, false otherwise.
bool isDigit(int32_t ch);

/// \brief Check if a character is an octal digit.
/// \param c The character to check.
/// \return True if the character is an octal digit, false otherwise.
bool isOctalDigit(zc::byte c);

/// \brief Check if a character is a hexadecimal digit.
/// \param ch The character to check.
/// \return True if the character is a hexadecimal digit, false otherwise.
bool isHexDigit(int32_t ch);

/// \brief Check if a character is a single-line white space.
/// \param code The character code to check.
/// \return True if the character is a single-line white space, false otherwise.
bool isWhiteSpaceSingleLine(uint32_t code);

/// \brief Check if a character is a line break.
/// \param code The character code to check.
/// \return True if the character is a line break, false otherwise.
bool isLineBreak(uint32_t code);

// =======================================================================================
// Keyword Utilities

/// \brief Check if a token kind corresponds to a keyword.
/// \param tokenKind The token kind to check.
/// \return True if the token kind is a keyword, false otherwise.
bool isKeyword(ast::SyntaxKind tokenKind);

/// \brief Check if a token kind corresponds to a reserved keyword.
/// \param tokenKind The token kind to check.
/// \return True if the token kind is a reserved keyword, false otherwise.
bool isReservedKeyword(ast::SyntaxKind tokenKind);

/// \brief Check if a token kind is an identifier or a keyword.
/// \param tokenKind The token kind to check.
/// \return True if the token kind is an identifier or a keyword, false otherwise.
bool isIdentifierOrKeyword(ast::SyntaxKind tokenKind);

/// \brief Get the syntax kind for a given keyword text.
/// \param text The text to check.
/// \return The syntax kind if it is a keyword, or Identifier otherwise.
ast::SyntaxKind getKeywordKind(zc::ArrayPtr<const zc::byte> text);

/// \brief Check if a string is a valid identifier.
/// \param s The string to check.
/// \return True if the string is a valid identifier, false otherwise.
bool isValidIdentifier(zc::StringPtr s);

// =======================================================================================
// Octal escape sequence parsing utilities

namespace _ {  // private implementation details

struct ParseOctalValue {
  uint32_t operator()(const zc::ArrayPtr<const zc::byte>& octalDigits) const;
};

struct FormatHexValue {
  zc::String operator()(uint32_t value) const;
};

}  // namespace _

/// \brief Parse an octal value from a sequence of digits.
/// \param octalDigits The octal digits to parse.
/// \return The parsed value.
uint32_t parseOctalValue(const zc::ArrayPtr<const zc::byte>& octalDigits);

/// \brief Format a value as a hexadecimal string.
/// \param value The value to format.
/// \return The hexadecimal string representation.
zc::String formatHexValue(uint32_t value);

/// \brief Parse an octal value and format it as a hexadecimal string.
/// \param octalDigits The octal digits to parse.
/// \return The hexadecimal string representation.
zc::String parseOctalAndFormatHex(const zc::ArrayPtr<const zc::byte>& octalDigits);

/// \brief Parse a hexadecimal value from a string.
/// \param hexDigits The hexadecimal digits to parse.
/// \return The parsed value.
uint32_t parseHexValue(zc::StringPtr hexDigits);

// =======================================================================================
// UTF-8 Utilities

/// \brief Encode a code point as UTF-8.
/// \param code The code point to encode.
/// \return The UTF-8 encoded string.
zc::String encodeUtf8(uint32_t code);

/// \brief Try to decode a UTF-8 code point from a byte sequence.
/// \param ptr Pointer to the start of the sequence.
/// \param end Pointer to the end of the sequence.
/// \return A pair containing the code point and its size in bytes.
std::pair<uint32_t, uint32_t> tryDecodeUtf8CodePoint(const zc::byte* ptr, const zc::byte* end);

/// \brief Decode a UTF-8 code point and its size.
/// \param ptr Pointer to the start of the sequence.
/// \param end Pointer to the end of the sequence.
/// \return A pair containing the code point and its size in bytes.
std::pair<uint32_t, uint32_t> charWithSize(const zc::byte* ptr, const zc::byte* end);

}  // namespace lexer
}  // namespace compiler
}  // namespace zomlang
