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

#include "zomlang/compiler/lexer/token.h"

#include "zc/core/memory.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang {
namespace compiler {
namespace lexer {

// ================================================================================
// Token::Impl

struct Token::Impl {
  TokenKind kind;
  source::SourceRange range;
  zc::Maybe<zc::String> cachedText;

  Impl() : kind(TokenKind::kUnknown) {}
  Impl(TokenKind k, source::SourceRange r, zc::Maybe<zc::String> text)
      : kind(k), range(r), cachedText(zc::mv(text)) {}
};

// ================================================================================
// Token

Token::Token() noexcept : impl(zc::heap<Impl>()) {}

Token::Token(TokenKind k, source::SourceRange r, zc::Maybe<zc::String> text) noexcept
    : impl(zc::heap<Impl>(k, r, zc::mv(text))) {}

Token::Token(const Token& other) noexcept : impl(zc::heap<Impl>()) {
  impl->kind = other.impl->kind;
  impl->range = other.impl->range;
  ZC_IF_SOME(text, other.impl->cachedText) { impl->cachedText = zc::str(text); }
}

Token::Token(Token&& other) noexcept : impl(zc::mv(other.impl)) {}

Token::~Token() noexcept(false) = default;

Token& Token::operator=(const Token& other) noexcept {
  if (this != &other) {
    impl->kind = other.impl->kind;
    impl->range = other.impl->range;
    ZC_IF_SOME(text, other.impl->cachedText) { impl->cachedText = zc::str(text); }
    else { impl->cachedText = zc::none; }
  }
  return *this;
}

Token& Token::operator=(Token&& other) noexcept {
  if (this != &other) {
    impl->kind = other.impl->kind;
    impl->range = zc::mv(other.impl->range);
    impl->cachedText = zc::mv(other.impl->cachedText);
  }
  return *this;
}

void Token::setKind(TokenKind k) { impl->kind = k; }

void Token::setRange(source::SourceRange r) { impl->range = r; }

void Token::setCachedText(zc::String text) { impl->cachedText = zc::mv(text); }

bool Token::is(TokenKind k) const { return impl->kind == k; }

TokenKind Token::getKind() const { return impl->kind; }

source::SourceLoc Token::getLocation() const { return impl->range.getStart(); }

source::SourceRange Token::getRange() const { return impl->range; }

zc::String Token::getText(const source::SourceManager& sm) const {
  // Fast path: return cached text if available
  ZC_IF_SOME(cached, impl->cachedText) { return zc::str(cached); }

  // Fast path: for known keywords and operators, return static strings
  ZC_IF_SOME(staticText, getStaticTextForTokenKind(impl->kind)) { return zc::str(staticText); }

  // Slow path: extract from source manager
  return impl->range.getText(sm);
}

namespace {
constexpr zc::StringPtr getStaticTextForTokenKindImpl(TokenKind kind) {
  switch (kind) {
    // Keywords
    case TokenKind::kLetKeyword:
      return "let"_zc;
    case TokenKind::kConstKeyword:
      return "const"_zc;
    case TokenKind::kVarKeyword:
      return "var"_zc;
    case TokenKind::kFunKeyword:
      return "fun"_zc;
    case TokenKind::kClassKeyword:
      return "class"_zc;
    case TokenKind::kIfKeyword:
      return "if"_zc;
    case TokenKind::kElseKeyword:
      return "else"_zc;
    case TokenKind::kForKeyword:
      return "for"_zc;
    case TokenKind::kWhileKeyword:
      return "while"_zc;
    case TokenKind::kReturnKeyword:
      return "return"_zc;
    case TokenKind::kBreakKeyword:
      return "break"_zc;
    case TokenKind::kContinueKeyword:
      return "continue"_zc;
    case TokenKind::kTrueKeyword:
      return "true"_zc;
    case TokenKind::kFalseKeyword:
      return "false"_zc;
    case TokenKind::kNullKeyword:
      return "null"_zc;
    case TokenKind::kNilKeyword:
      return "nil"_zc;
    case TokenKind::kThisKeyword:
      return "this"_zc;
    case TokenKind::kSuperKeyword:
      return "super"_zc;
    case TokenKind::kNewKeyword:
      return "new"_zc;
    case TokenKind::kTryKeyword:
      return "try"_zc;
    case TokenKind::kCatchKeyword:
      return "catch"_zc;
    case TokenKind::kFinallyKeyword:
      return "finally"_zc;
    case TokenKind::kThrowKeyword:
      return "throw"_zc;
    case TokenKind::kTypeOfKeyword:
      return "typeof"_zc;
    case TokenKind::kVoidKeyword:
      return "void"_zc;
    case TokenKind::kDeleteKeyword:
      return "delete"_zc;
    case TokenKind::kInKeyword:
      return "in"_zc;
    case TokenKind::kOfKeyword:
      return "of"_zc;
    case TokenKind::kInstanceOfKeyword:
      return "instanceof"_zc;
    case TokenKind::kAsKeyword:
      return "as"_zc;
    case TokenKind::kIsKeyword:
      return "is"_zc;
    case TokenKind::kImportKeyword:
      return "import"_zc;
    case TokenKind::kExportKeyword:
      return "export"_zc;
    case TokenKind::kFromKeyword:
      return "from"_zc;
    case TokenKind::kDefaultKeyword:
      return "default"_zc;
    case TokenKind::kAsyncKeyword:
      return "async"_zc;
    case TokenKind::kAwaitKeyword:
      return "await"_zc;
    case TokenKind::kYieldKeyword:
      return "yield"_zc;
    case TokenKind::kStaticKeyword:
      return "static"_zc;
    case TokenKind::kPublicKeyword:
      return "public"_zc;
    case TokenKind::kPrivateKeyword:
      return "private"_zc;
    case TokenKind::kProtectedKeyword:
      return "protected"_zc;
    case TokenKind::kAbstractKeyword:
      return "abstract"_zc;
    case TokenKind::kOverrideKeyword:
      return "override"_zc;
    case TokenKind::kInterfaceKeyword:
      return "interface"_zc;
    case TokenKind::kImplementsKeyword:
      return "implements"_zc;
    case TokenKind::kExtendsKeyword:
      return "extends"_zc;
    case TokenKind::kStructKeyword:
      return "struct"_zc;
    case TokenKind::kEnumKeyword:
      return "enum"_zc;
    case TokenKind::kErrorKeyword:
      return "error"_zc;
    case TokenKind::kAliasKeyword:
      return "alias"_zc;
    case TokenKind::kTypeKeyword:
      return "type"_zc;
    case TokenKind::kNamespaceKeyword:
      return "namespace"_zc;
    case TokenKind::kModuleKeyword:
      return "module"_zc;
    case TokenKind::kPackageKeyword:
      return "package"_zc;
    case TokenKind::kUsingKeyword:
      return "using"_zc;
    case TokenKind::kWithKeyword:
      return "with"_zc;
    case TokenKind::kWhenKeyword:
      return "when"_zc;
    case TokenKind::kSwitchKeyword:
      return "switch"_zc;
    case TokenKind::kCaseKeyword:
      return "case"_zc;
    case TokenKind::kMatchKeyword:
      return "match"_zc;
    case TokenKind::kDoKeyword:
      return "do"_zc;
    case TokenKind::kDebuggerKeyword:
      return "debugger"_zc;
    case TokenKind::kInitKeyword:
      return "init"_zc;
    case TokenKind::kDeinitKeyword:
      return "deinit"_zc;
    case TokenKind::kRaisesKeyword:
      return "raises"_zc;

    // Type keywords
    case TokenKind::kBoolKeyword:
      return "bool"_zc;
    case TokenKind::kI8Keyword:
      return "i8"_zc;
    case TokenKind::kI32Keyword:
      return "i32"_zc;
    case TokenKind::kI64Keyword:
      return "i64"_zc;
    case TokenKind::kU8Keyword:
      return "u8"_zc;
    case TokenKind::kU16Keyword:
      return "u16"_zc;
    case TokenKind::kU32Keyword:
      return "u32"_zc;
    case TokenKind::kU64Keyword:
      return "u64"_zc;
    case TokenKind::kF32Keyword:
      return "f32"_zc;
    case TokenKind::kF64Keyword:
      return "f64"_zc;
    case TokenKind::kStrKeyword:
      return "str"_zc;
    case TokenKind::kUnitKeyword:
      return "unit"_zc;

    // Common operators
    case TokenKind::kPlus:
      return "+"_zc;
    case TokenKind::kMinus:
      return "-"_zc;
    case TokenKind::kAsterisk:
      return "*"_zc;
    case TokenKind::kAsteriskAsterisk:
      return "**"_zc;
    case TokenKind::kSlash:
      return "/"_zc;
    case TokenKind::kPercent:
      return "%"_zc;
    case TokenKind::kEquals:
      return "="_zc;
    case TokenKind::kEqualsEquals:
      return "=="_zc;
    case TokenKind::kEqualsEqualsEquals:
      return "==="_zc;
    case TokenKind::kExclamationEquals:
      return "!="_zc;
    case TokenKind::kExclamationEqualsEquals:
      return "!=="_zc;
    case TokenKind::kLessThan:
      return "<"_zc;
    case TokenKind::kGreaterThan:
      return ">"_zc;
    case TokenKind::kLessThanEquals:
      return "<="_zc;
    case TokenKind::kGreaterThanEquals:
      return ">="_zc;
    case TokenKind::kAmpersandAmpersand:
      return "&&"_zc;
    case TokenKind::kBarBar:
      return "||"_zc;
    case TokenKind::kExclamation:
      return "!"_zc;
    case TokenKind::kQuestion:
      return "?"_zc;
    case TokenKind::kColon:
      return ":"_zc;
    case TokenKind::kSemicolon:
      return ";"_zc;
    case TokenKind::kComma:
      return ","_zc;
    case TokenKind::kPeriod:
      return "."_zc;
    case TokenKind::kArrow:
      return "->"_zc;
    case TokenKind::kEqualsGreaterThan:
      return "=>"_zc;
    case TokenKind::kPlusPlus:
      return "++"_zc;
    case TokenKind::kMinusMinus:
      return "--"_zc;
    case TokenKind::kPlusEquals:
      return "+="_zc;
    case TokenKind::kMinusEquals:
      return "-="_zc;
    case TokenKind::kAsteriskEquals:
      return "*="_zc;
    case TokenKind::kAsteriskAsteriskEquals:
      return "**="_zc;
    case TokenKind::kSlashEquals:
      return "/="_zc;
    case TokenKind::kPercentEquals:
      return "%="_zc;
    case TokenKind::kAmpersand:
      return "&"_zc;
    case TokenKind::kBar:
      return "|"_zc;
    case TokenKind::kCaret:
      return "^"_zc;
    case TokenKind::kTilde:
      return "~"_zc;
    case TokenKind::kLessThanLessThan:
      return "<<"_zc;
    case TokenKind::kLessThanSlash:
      return "</"_zc;
    case TokenKind::kGreaterThanGreaterThan:
      return ">>"_zc;
    case TokenKind::kGreaterThanGreaterThanGreaterThan:
      return ">"
             ">"
             ">"_zc;
    case TokenKind::kAmpersandEquals:
      return "&="_zc;
    case TokenKind::kBarEquals:
      return "|="_zc;
    case TokenKind::kCaretEquals:
      return "^="_zc;
    case TokenKind::kLessThanLessThanEquals:
      return "<<="_zc;
    case TokenKind::kGreaterThanGreaterThanEquals:
      return ">>="_zc;
    case TokenKind::kGreaterThanGreaterThanGreaterThanEquals:
      return ">>>="_zc;
    case TokenKind::kBarBarEquals:
      return "||="_zc;
    case TokenKind::kAmpersandAmpersandEquals:
      return "&&="_zc;
    case TokenKind::kQuestionQuestion:
      return "??"_zc;
    case TokenKind::kQuestionQuestionEquals:
      return "\?\?="_zc;
    case TokenKind::kQuestionDot:
      return "?."_zc;
    case TokenKind::kDotDotDot:
      return "..."_zc;
    case TokenKind::kErrorPropagate:
      return "?!"_zc;
    case TokenKind::kErrorUnwrap:
      return "!!"_zc;
    case TokenKind::kErrorDefault:
      return "?:"_zc;

    // Punctuation
    case TokenKind::kLeftParen:
      return "("_zc;
    case TokenKind::kRightParen:
      return ")"_zc;
    case TokenKind::kLeftBrace:
      return "{"_zc;
    case TokenKind::kRightBrace:
      return "}"_zc;
    case TokenKind::kLeftBracket:
      return "["_zc;
    case TokenKind::kRightBracket:
      return "]"_zc;
    case TokenKind::kAt:
      return "@"_zc;
    case TokenKind::kHash:
      return "#"_zc;
    case TokenKind::kBacktick:
      return "`"_zc;

    default:
      return ""_zc;
  }
}
}  // namespace

zc::Maybe<zc::String> Token::getStaticTextForTokenKind(TokenKind kind) {
  zc::StringPtr text = getStaticTextForTokenKindImpl(kind);
  if (text.size() > 0) { return zc::str(text); }
  return zc::none;
}

zc::String Token::getTextWithBufferHint(const source::SourceManager& sm,
                                        const void* bufferIdPtr) const {
  // Fast path: return cached text if available
  ZC_IF_SOME(cached, impl->cachedText) { return zc::str(cached); }

  // Fast path: for known keywords and operators, return static strings
  ZC_IF_SOME(staticText, getStaticTextForTokenKind(impl->kind)) { return zc::str(staticText); }

  // Optimized path: extract with buffer hint if provided
  if (bufferIdPtr != nullptr) {
    const source::BufferId* bufferId = static_cast<const source::BufferId*>(bufferIdPtr);
    zc::ArrayPtr<const zc::byte> textBytes = sm.extractTextFast(impl->range, *bufferId);
    return zc::str(textBytes.asChars());
  }

  // Fallback to slow path
  return impl->range.getText(sm);
}

}  // namespace lexer
}  // namespace compiler
}  // namespace zomlang
