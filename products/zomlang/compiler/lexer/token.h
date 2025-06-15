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

#include "zc/core/string.h"
#include "zomlang/compiler/source/location.h"

namespace zomlang {
namespace compiler {
namespace lexer {

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
  kBooleanKeyword,      // boolean
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
  kNumberKeyword,       // number
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

  // Boolean and null literals
  kTrueKeyword,         // true
  kFalseKeyword,        // false
  kNullKeyword,         // null

  // Literals
  kIntegerLiteral,
  kFloatLiteral,
  kStringLiteral,
  kBooleanLiteral,      // true/false
  kNullLiteral,         // null

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
  Token() : kind(TokenKind::kUnknown) {}
  Token(TokenKind k, source::SourceRange r) : kind(k), range(r) {}

  // Copy constructor
  Token(const Token& other) : kind(other.kind), range(other.range) {}

  // Move constructor (defaulted is fine as source::SourceRange is movable)
  Token(Token&& other) noexcept = default;

  ~Token() = default;

  // Copy assignment operator
  Token& operator=(const Token& other) {
    if (this != &other) {
      kind = other.kind;
      range = other.range;
    }
    return *this;
  }

  // Move assignment operator (defaulted is fine)
  Token& operator=(Token&& other) noexcept = default;

  void setKind(TokenKind k) { kind = k; }
  void setRange(source::SourceRange r) { range = r; }

  ZC_NODISCARD bool is(TokenKind k) const { return kind == k; }

  ZC_NODISCARD TokenKind getKind() const { return kind; }
  ZC_NODISCARD source::SourceLoc getLocation() const { return range.getStart(); }
  ZC_NODISCARD source::SourceRange getRange() const { return range; }

  /// Get the raw text content of this token
  ZC_NODISCARD zc::String getText(const source::SourceManager& sm) const {
    return range.getText(sm);
  }

private:
  TokenKind kind;
  source::SourceRange range;
};

}  // namespace lexer
}  // namespace compiler
}  // namespace zomlang
