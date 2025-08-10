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

enum class TokenKind {
  kUnknown,

  // Identifiers
  kIdentifier,

  // Keywords
  kKeywords,
  kAbstractKeyword,     // abstract
  kAccessorKeyword,     // accessor
  kAnyKeyword,          // any
  kAsKeyword,           // as
  kAssertsKeyword,      // asserts
  kAssertKeyword,       // assert
  kAsyncKeyword,        // async
  kAwaitKeyword,        // await
  kBigIntKeyword,       // bigint
  kBreakKeyword,        // break
  kCaseKeyword,         // case
  kCatchKeyword,        // catch
  kClassKeyword,        // class
  kContinueKeyword,     // continue
  kConstKeyword,        // const
  kConstructorKeyword,  // constructor
  kDebuggerKeyword,     // debugger
  kDeclareKeyword,      // declare
  kDefaultKeyword,      // default
  kDeleteKeyword,       // delete
  kDoKeyword,           // do
  kExtendsKeyword,      // extends
  kExportKeyword,       // export
  kFinallyKeyword,      // finally
  kFromKeyword,         // from
  kFunKeyword,          // fun
  kGetKeyword,          // get
  kGlobalKeyword,       // global
  kIfKeyword,           // if
  kImmediateKeyword,    // immediate
  kImplementsKeyword,   // implements
  kImportKeyword,       // import
  kInKeyword,           // in
  kInferKeyword,        // infer
  kInstanceOfKeyword,   // instanceof
  kInterfaceKeyword,    // interface
  kIntrinsicKeyword,    // intrinsic
  kIsKeyword,           // is
  kKeyOfKeyword,        // keyof
  kLetKeyword,          // let
  kMatchKeyword,        // match
  kModuleKeyword,       // module
  kMutableKeyword,      // mutable
  kNamespaceKeyword,    // namespace
  kNeverKeyword,        // never
  kNewKeyword,          // new
  kObjectKeyword,       // object
  kOfKeyword,           // of
  kOptionalKeyword,     // optional
  kOutKeyword,          // out
  kOverrideKeyword,     // override
  kPackageKeyword,      // package
  kPrivateKeyword,      // private
  kProtectedKeyword,    // protected
  kPublicKeyword,       // public
  kReadonlyKeyword,     // readonly
  kRequireKeyword,      // require
  kReturnKeyword,       // return
  kSatisfiesKeyword,    // satisfies
  kSetKeyword,          // set
  kStaticKeyword,       // static
  kSuperKeyword,        // super
  kSwitchKeyword,       // switch
  kSymbolKeyword,       // symbol
  kThisKeyword,         // this
  kThrowKeyword,        // throw
  kTryKeyword,          // try
  kTypeOfKeyword,       // typeof
  kUndefinedKeyword,    // undefined
  kUniqueKeyword,       // unique
  kUsingKeyword,        // using
  kVarKeyword,          // var
  kVoidKeyword,         // void
  kWhenKeyword,         // when
  kWithKeyword,         // with
  kYieldKeyword,        // yield

  // Type keywords
  kBoolKeyword,    // bool
  kI8Keyword,      // i8
  kI32Keyword,     // i32
  kI64Keyword,     // i64
  kU8Keyword,      // u8
  kU16Keyword,     // u16
  kU32Keyword,     // u32
  kU64Keyword,     // u64
  kF32Keyword,     // f32
  kF64Keyword,     // f64
  kStrKeyword,     // str
  kUnitKeyword,    // unit
  kElseKeyword,    // else
  kForKeyword,     // for
  kWhileKeyword,   // while
  kStructKeyword,  // struct
  kEnumKeyword,    // enum
  kErrorKeyword,   // error
  kAliasKeyword,   // alias
  kInitKeyword,    // init
  kDeinitKeyword,  // deinit
  kRaisesKeyword,  // raises
  kTypeKeyword,    // type

  // Boolean and null literals
  kTrueKeyword,   // true
  kFalseKeyword,  // false
  kNullKeyword,   // null

  // Literals
  kIntegerLiteral,
  kFloatLiteral,
  kStringLiteral,
  kCharacterLiteral,
  kBooleanLiteral,  // true/false
  kNullLiteral,     // null

  // Operators
  kOperator,
  kArrow,                                    // ->
  kColon,                                    // :
  kPeriod,                                   // .
  kDotDotDot,                                // ...
  kLessThan,                                 // <
  kGreaterThan,                              // >
  kLessThanEquals,                           // <=
  kGreaterThanEquals,                        // >=
  kEqualsEquals,                             // ==
  kExclamationEquals,                        // !=
  kEqualsEqualsEquals,                       // ===
  kExclamationEqualsEquals,                  // !==
  kEqualsGreaterThan,                        // =>
  kPlus,                                     // +
  kMinus,                                    // -
  kAsteriskAsterisk,                         // **
  kAsterisk,                                 // *
  kSlash,                                    // /
  kPercent,                                  // %
  kPlusPlus,                                 // ++
  kMinusMinus,                               // --
  kLessThanLessThan,                         // <<
  kLessThanSlash,                            // </
  kGreaterThanGreaterThan,                   // >>
  kGreaterThanGreaterThanGreaterThan,        // >>>
  kAmpersand,                                // &
  kBar,                                      // |
  kCaret,                                    // ^
  kExclamation,                              // !
  kTilde,                                    // ~
  kAmpersandAmpersand,                       // &&
  kBarBar,                                   // ||
  kQuestion,                                 // ?
  kQuestionQuestion,                         // ??
  kQuestionDot,                              // ?.
  kEquals,                                   // =
  kPlusEquals,                               // +=
  kMinusEquals,                              // -=
  kAsteriskEquals,                           // *=
  kAsteriskAsteriskEquals,                   // **=
  kSlashEquals,                              // /=
  kPercentEquals,                            // %=
  kLessThanLessThanEquals,                   // <<=
  kGreaterThanGreaterThanEquals,             // >>=
  kGreaterThanGreaterThanGreaterThanEquals,  // >>>=
  kAmpersandEquals,                          // &=
  kBarEquals,                                // |=
  kCaretEquals,                              // ^=
  kBarBarEquals,                             // ||=
  kAmpersandAmpersandEquals,                 // &&=
  kQuestionQuestionEquals,                   // ??=
  kErrorPropagate,                           // ?!
  kErrorUnwrap,                              // !!
  kErrorDefault,                             // ?:
  kAt,                                       // @
  kHash,                                     // #
  kBacktick,                                 // `

  // Punctuation
  kPunctuation,
  kLeftParen,     // (
  kRightParen,    // )
  kLeftBrace,     // {
  kRightBrace,    // }
  kSemicolon,     // ;
  kComma,         // ,
  kLeftBracket,   // [
  kRightBracket,  // ]

  kComment,

  // Add more token types as needed...

  kEOF,
};

class Token {
public:
  Token() noexcept;
  Token(TokenKind k, source::SourceRange r, zc::Maybe<zc::String> text = zc::none,
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

  void setKind(TokenKind k);
  void setRange(source::SourceRange r);
  void setCachedText(zc::String text);
  void setFlags(TokenFlags flags);
  void addFlag(TokenFlags flag);

  ZC_NODISCARD bool is(TokenKind k) const;

  ZC_NODISCARD TokenKind getKind() const;
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
  static zc::Maybe<zc::String> getStaticTextForTokenKind(TokenKind kind);

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace lexer
}  // namespace compiler
}  // namespace zomlang
