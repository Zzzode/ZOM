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
  kNone = 0,
  // Line break flags
  kPrecedingLineBreak = 1 << 0,

  // String/escape sequence flags
  kUnterminated = 1 << 1,
  kExtendedUnicodeEscape = 1 << 2,  // e.g. `\u{10ffff}`
  kUnicodeEscape = 1 << 3,          // e.g. `\u00a0`
  kHexEscape = 1 << 4,              // e.g. `\xa0`
  kContainsInvalidEscape = 1 << 5,  // e.g. `\uhello`

  // Numeric literal flags
  kScientific = 1 << 6,                 // e.g. `10e2`
  kOctal = 1 << 7,                      // e.g. `0777`
  kHexSpecifier = 1 << 8,               // e.g. `0x00000000`
  kBinarySpecifier = 1 << 9,            // e.g. `0b0110010000000000`
  kOctalSpecifier = 1 << 10,            // e.g. `0o777`
  kContainsSeparator = 1 << 11,         // e.g. `0b1100_0101`
  kContainsLeadingZero = 1 << 12,       // e.g. `0888`
  kContainsInvalidSeparator = 1 << 13,  // e.g. `0_1`

  // Composite flags for convenience
  kBinaryOrOctalSpecifier = kBinarySpecifier | kOctalSpecifier,
  kWithSpecifier = kHexSpecifier | kBinaryOrOctalSpecifier,
  kStringLiteralFlags =
      kHexEscape | kUnicodeEscape | kExtendedUnicodeEscape | kContainsInvalidEscape,
  kNumericLiteralFlags = kScientific | kOctal | kContainsLeadingZero | kWithSpecifier |
                         kContainsSeparator | kContainsInvalidSeparator,
  kTemplateLiteralLikeFlags =
      kHexEscape | kUnicodeEscape | kExtendedUnicodeEscape | kContainsInvalidEscape,
  kIsInvalid = kOctal | kContainsLeadingZero | kContainsInvalidSeparator | kContainsInvalidEscape,
};

class Token {
public:
  Token() noexcept;
  Token(ast::SyntaxKind k, source::SourceRange r, zc::Maybe<zc::String> text = zc::none,
        TokenFlags flags = TokenFlags::kNone) noexcept;

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
  void setCachedText(zc::String text);
  void setFlags(TokenFlags flags);
  void addFlag(TokenFlags flag);

  ZC_NODISCARD bool is(ast::SyntaxKind k) const;

  ZC_NODISCARD ast::SyntaxKind getKind() const;
  ZC_NODISCARD source::SourceLoc getLocation() const;
  ZC_NODISCARD source::SourceRange getRange() const;
  ZC_NODISCARD TokenFlags getFlags() const;
  ZC_NODISCARD bool hasFlag(TokenFlags flag) const;
  ZC_NODISCARD bool hasPrecedingLineBreak() const;

  /// Get the raw text content of this token with fast path optimization
  ZC_NODISCARD zc::String getText(const source::SourceManager& sm) const;

  /// Get text with buffer hint for better performance
  /// Implementation in token.cc to avoid incomplete type issues
  ZC_NODISCARD zc::String getTextWithBufferHint(const source::SourceManager& sm,
                                                const void* bufferIdPtr) const;

  /// Get static text for common keywords and operators
  static zc::Maybe<zc::String> getStaticTextForTokenKind(ast::SyntaxKind kind);

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace lexer
}  // namespace compiler
}  // namespace zomlang
