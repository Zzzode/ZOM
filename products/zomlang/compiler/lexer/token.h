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

#include "zc/core/common.h"
#include "zc/core/string.h"
#include "zomlang/compiler/ast/kinds.h"
#include "zomlang/compiler/source/location.h"

namespace zomlang {
namespace compiler {

namespace source {
class BufferId;
class SourceManager;
}  // namespace source

namespace lexer {

enum class TokenFlags : uint16_t {
  None = 0,
  // Line break flags
  PrecedingLineBreak = 1 << 0,

  // String/escape sequence flags
  Unterminated = 1 << 1,
  ExtendedUnicodeEscape = 1 << 2,  // e.g. `\u{10ffff}`
  UnicodeEscape = 1 << 3,          // e.g. `\u00a0`
  HexEscape = 1 << 4,              // e.g. `\xa0`
  ContainsInvalidEscape = 1 << 5,  // e.g. `\uhello`

  // Numeric literal flags
  Scientific = 1 << 6,                 // e.g. `10e2`
  Octal = 1 << 7,                      // e.g. `0777`
  HexSpecifier = 1 << 8,               // e.g. `0x00000000`
  BinarySpecifier = 1 << 9,            // e.g. `0b0110010000000000`
  OctalSpecifier = 1 << 10,            // e.g. `0o777`
  ContainsSeparator = 1 << 11,         // e.g. `0b1100_0101`
  ContainsLeadingZero = 1 << 12,       // e.g. `0888`
  ContainsInvalidSeparator = 1 << 13,  // e.g. `0_1`

  // Composite flags for convenience
  BinaryOrOctalSpecifier = BinarySpecifier | OctalSpecifier,
  WithSpecifier = HexSpecifier | BinaryOrOctalSpecifier,
  StringLiteralFlags = HexEscape | UnicodeEscape | ExtendedUnicodeEscape | ContainsInvalidEscape,
  NumericLiteralFlags = Scientific | Octal | ContainsLeadingZero | WithSpecifier |
                        ContainsSeparator | ContainsInvalidSeparator,
  TemplateLiteralLikeFlags =
      HexEscape | UnicodeEscape | ExtendedUnicodeEscape | ContainsInvalidEscape,
  IsInvalid = Octal | ContainsLeadingZero | ContainsInvalidSeparator | ContainsInvalidEscape,
};

constexpr TokenFlags operator|(TokenFlags lhs, TokenFlags rhs) {
  return static_cast<TokenFlags>(static_cast<uint16_t>(lhs) | static_cast<uint16_t>(rhs));
}
constexpr TokenFlags operator&(TokenFlags lhs, TokenFlags rhs) {
  return static_cast<TokenFlags>(static_cast<uint16_t>(lhs) & static_cast<uint16_t>(rhs));
}
constexpr TokenFlags operator^(TokenFlags lhs, TokenFlags rhs) {
  return static_cast<TokenFlags>(static_cast<uint16_t>(lhs) ^ static_cast<uint16_t>(rhs));
}
constexpr TokenFlags operator~(TokenFlags v) {
  return static_cast<TokenFlags>(~static_cast<uint16_t>(v));
}
inline TokenFlags& operator|=(TokenFlags& lhs, TokenFlags rhs) {
  lhs = lhs | rhs;
  return lhs;
}
inline TokenFlags& operator&=(TokenFlags& lhs, TokenFlags rhs) {
  lhs = lhs & rhs;
  return lhs;
}
inline TokenFlags& operator^=(TokenFlags& lhs, TokenFlags rhs) {
  lhs = lhs ^ rhs;
  return lhs;
}

constexpr bool hasFlag(TokenFlags flags, TokenFlags flag) {
  return (flags & flag) != TokenFlags::None;
}

class Token {
public:
  Token() noexcept;
  Token(ast::SyntaxKind k, source::SourceRange r, zc::Maybe<zc::StringPtr> value = zc::none,
        TokenFlags flags = TokenFlags::None) noexcept;

  // Copy constructor
  Token(const Token& other) noexcept;

  // Move constructor
  Token(Token&& other) noexcept;

  ~Token() noexcept(false);

  // Copy assignment operator
  Token& operator=(const Token& other) noexcept;

  // Move assignment operator
  Token& operator=(Token&& other) noexcept;

  void setKind(ast::SyntaxKind k);
  void setRange(source::SourceRange r);
  void setValue(zc::StringPtr value);
  void setFlags(TokenFlags flags);
  void addFlag(TokenFlags flag);

  ZC_NODISCARD bool is(ast::SyntaxKind k) const;

  ZC_NODISCARD ast::SyntaxKind getKind() const;
  ZC_NODISCARD source::SourceLoc getLocation() const;
  ZC_NODISCARD source::SourceRange getRange() const;
  ZC_NODISCARD TokenFlags getFlags() const;

  // Retrieve value if available
  zc::Maybe<zc::StringPtr> getValue() const;

  ZC_NODISCARD bool hasFlag(TokenFlags flag) const;
  ZC_NODISCARD bool hasPrecedingLineBreak() const;

  /// Get the raw text content of this token with fast path optimization
  ZC_NODISCARD zc::StringPtr getText(const source::SourceManager& sm) const;

  /// Get text with buffer hint for better performance
  /// Implementation in token.cc to avoid incomplete type issues
  ZC_NODISCARD zc::StringPtr getTextWithBufferHint(const source::SourceManager& sm,
                                                   const void* bufferIdPtr) const;

  /// Get static text for common keywords and operators
  static zc::Maybe<zc::StringPtr> getStaticTextForTokenKind(ast::SyntaxKind kind);

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace lexer
}  // namespace compiler
}  // namespace zomlang
