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
  kConstructorKeyword,  // constructor
  kDebuggerKeyword,     // debugger
  kDeclareKeyword,      // declare
  kDefaultKeyword,      // default
  kDeleteKeyword,       // delete
  kDoKeyword,           // do
  kExtendsKeyword,      // extends
  kFinallyKeyword,      // finally
  kFromKeyword,         // from
  kFunKeyword,          // fun
  kGetKeyword,          // get
  kGlobalKeyword,       // global
  kImmediateKeyword,    // immediate
  kImplementsKeyword,   // implements
  kInKeyword,           // in
  kInferKeyword,        // infer
  kInstanceOfKeyword,   // instanceof
  kInterfaceKeyword,    // interface
  kIntrinsicKeyword,    // intrinsic
  kIsKeyword,           // is
  kKeyOfKeyword,        // keyof
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
  kWithKeyword,         // with
  kYieldKeyword,        // yield

  // Literals
  kIntegerLiteral,
  kFloatLiteral,
  kStringLiteral,

  // Operators
  kOperator,
  kArrow,                                    // ->
  kColon,                                    // :
  kDot,                                      // .
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
  Token() = default;
  Token(const TokenKind k, zc::StringPtr t, const source::SourceLoc l) : kind(k), text(t), loc(l) {}
  ~Token() = default;

  void setKind(const TokenKind k) { kind = k; }
  void setText(zc::StringPtr t) { text = t; }
  void setLocation(const source::SourceLoc l) { loc = l; }

  ZC_NODISCARD TokenKind getKind() const { return kind; }
  ZC_NODISCARD zc::StringPtr getText() const { return text; }
  ZC_NODISCARD unsigned getLength() const { return text.size(); }
  ZC_NODISCARD source::SourceLoc getLocation() const { return loc; }

private:
  TokenKind kind;
  zc::StringPtr text;
  source::SourceLoc loc;
};

}  // namespace lexer
}  // namespace compiler
}  // namespace zomlang
