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
  ast::SyntaxKind kind;
  source::SourceRange range;
  zc::Maybe<zc::String> cachedText;
  TokenFlags flags;

  Impl() : kind(ast::SyntaxKind::Unknown), flags(TokenFlags::kNone) {}
  Impl(ast::SyntaxKind k, source::SourceRange r, zc::Maybe<zc::String> text,
       TokenFlags f = TokenFlags::kNone)
      : kind(k), range(r), cachedText(zc::mv(text)), flags(f) {}
};

// ================================================================================
// Token

Token::Token() noexcept : impl(zc::heap<Impl>()) {}

Token::Token(ast::SyntaxKind k, source::SourceRange r, zc::Maybe<zc::String> text,
             TokenFlags flags) noexcept
    : impl(zc::heap<Impl>(k, r, zc::mv(text), flags)) {}

Token::Token(const Token& other) noexcept : impl(zc::heap<Impl>()) {
  impl->kind = other.impl->kind;
  impl->range = other.impl->range;
  impl->flags = other.impl->flags;
  ZC_IF_SOME(text, other.impl->cachedText) { impl->cachedText = zc::str(text); }
}

Token::Token(Token&& other) noexcept : impl(zc::mv(other.impl)) {}

Token::~Token() noexcept(false) = default;

Token& Token::operator=(const Token& other) noexcept {
  if (this != &other) {
    impl->kind = other.impl->kind;
    impl->range = other.impl->range;
    impl->flags = other.impl->flags;
    ZC_IF_SOME(text, other.impl->cachedText) { impl->cachedText = zc::str(text); }
    else { impl->cachedText = zc::none; }
  }
  return *this;
}

Token& Token::operator=(Token&& other) noexcept {
  if (this != &other) {
    impl->kind = other.impl->kind;
    impl->range = zc::mv(other.impl->range);
    impl->flags = other.impl->flags;
    impl->cachedText = zc::mv(other.impl->cachedText);
  }
  return *this;
}

void Token::setKind(ast::SyntaxKind k) { impl->kind = k; }

void Token::setRange(source::SourceRange r) { impl->range = r; }

void Token::setCachedText(zc::String text) { impl->cachedText = zc::mv(text); }

void Token::setFlags(TokenFlags flags) { impl->flags = flags; }

void Token::addFlag(TokenFlags flag) {
  impl->flags =
      static_cast<TokenFlags>(static_cast<uint8_t>(impl->flags) | static_cast<uint8_t>(flag));
}

bool Token::is(ast::SyntaxKind k) const { return impl->kind == k; }

ast::SyntaxKind Token::getKind() const { return impl->kind; }

source::SourceLoc Token::getLocation() const { return impl->range.getStart(); }

source::SourceRange Token::getRange() const { return impl->range; }

TokenFlags Token::getFlags() const { return impl->flags; }

bool Token::hasFlag(TokenFlags flag) const {
  return (static_cast<uint8_t>(impl->flags) & static_cast<uint8_t>(flag)) != 0;
}

bool Token::hasPrecedingLineBreak() const { return hasFlag(TokenFlags::kPrecedingLineBreak); }

zc::String Token::getText(const source::SourceManager& sm) const {
  // Fast path: return cached text if available
  ZC_IF_SOME(cached, impl->cachedText) { return zc::str(cached); }

  // Fast path: for known keywords and operators, return static strings
  ZC_IF_SOME(staticText, getStaticTextForTokenKind(impl->kind)) { return zc::str(staticText); }

  // Slow path: extract from source manager
  return impl->range.getText(sm);
}

namespace {
constexpr zc::StringPtr getStaticTextForTokenKindImpl(ast::SyntaxKind kind) {
  switch (kind) {
    // Keywords
    case ast::SyntaxKind::LetKeyword:
      return "let"_zc;
    case ast::SyntaxKind::ConstKeyword:
      return "const"_zc;
    case ast::SyntaxKind::VarKeyword:
      return "var"_zc;
    case ast::SyntaxKind::FunKeyword:
      return "fun"_zc;
    case ast::SyntaxKind::ClassKeyword:
      return "class"_zc;
    case ast::SyntaxKind::IfKeyword:
      return "if"_zc;
    case ast::SyntaxKind::ElseKeyword:
      return "else"_zc;
    case ast::SyntaxKind::ForKeyword:
      return "for"_zc;
    case ast::SyntaxKind::WhileKeyword:
      return "while"_zc;
    case ast::SyntaxKind::ReturnKeyword:
      return "return"_zc;
    case ast::SyntaxKind::BreakKeyword:
      return "break"_zc;
    case ast::SyntaxKind::ContinueKeyword:
      return "continue"_zc;
    case ast::SyntaxKind::TrueKeyword:
      return "true"_zc;
    case ast::SyntaxKind::FalseKeyword:
      return "false"_zc;
    case ast::SyntaxKind::NullKeyword:
      return "null"_zc;
    case ast::SyntaxKind::ThisKeyword:
      return "this"_zc;
    case ast::SyntaxKind::SuperKeyword:
      return "super"_zc;
    case ast::SyntaxKind::NewKeyword:
      return "new"_zc;
    case ast::SyntaxKind::TryKeyword:
      return "try"_zc;
    case ast::SyntaxKind::CatchKeyword:
      return "catch"_zc;
    case ast::SyntaxKind::FinallyKeyword:
      return "finally"_zc;
    case ast::SyntaxKind::ThrowKeyword:
      return "throw"_zc;
    case ast::SyntaxKind::TypeOfKeyword:
      return "typeof"_zc;
    case ast::SyntaxKind::VoidKeyword:
      return "void"_zc;
    case ast::SyntaxKind::DeleteKeyword:
      return "delete"_zc;
    case ast::SyntaxKind::InKeyword:
      return "in"_zc;
    case ast::SyntaxKind::OfKeyword:
      return "of"_zc;
    case ast::SyntaxKind::InstanceOfKeyword:
      return "instanceof"_zc;
    case ast::SyntaxKind::AsKeyword:
      return "as"_zc;
    case ast::SyntaxKind::IsKeyword:
      return "is"_zc;
    case ast::SyntaxKind::ImportKeyword:
      return "import"_zc;
    case ast::SyntaxKind::ExportKeyword:
      return "export"_zc;
    case ast::SyntaxKind::FromKeyword:
      return "from"_zc;
    case ast::SyntaxKind::DefaultKeyword:
      return "default"_zc;
    case ast::SyntaxKind::AsyncKeyword:
      return "async"_zc;
    case ast::SyntaxKind::AwaitKeyword:
      return "await"_zc;
    case ast::SyntaxKind::YieldKeyword:
      return "yield"_zc;
    case ast::SyntaxKind::StaticKeyword:
      return "static"_zc;
    case ast::SyntaxKind::PublicKeyword:
      return "public"_zc;
    case ast::SyntaxKind::PrivateKeyword:
      return "private"_zc;
    case ast::SyntaxKind::ProtectedKeyword:
      return "protected"_zc;
    case ast::SyntaxKind::AbstractKeyword:
      return "abstract"_zc;
    case ast::SyntaxKind::OverrideKeyword:
      return "override"_zc;
    case ast::SyntaxKind::InterfaceKeyword:
      return "interface"_zc;
    case ast::SyntaxKind::ImplementsKeyword:
      return "implements"_zc;
    case ast::SyntaxKind::ExtendsKeyword:
      return "extends"_zc;
    case ast::SyntaxKind::StructKeyword:
      return "struct"_zc;
    case ast::SyntaxKind::EnumKeyword:
      return "enum"_zc;
    case ast::SyntaxKind::ErrorKeyword:
      return "error"_zc;
    case ast::SyntaxKind::AliasKeyword:
      return "alias"_zc;
    case ast::SyntaxKind::TypeKeyword:
      return "type"_zc;
    case ast::SyntaxKind::NamespaceKeyword:
      return "namespace"_zc;
    case ast::SyntaxKind::ModuleKeyword:
      return "module"_zc;
    case ast::SyntaxKind::PackageKeyword:
      return "package"_zc;
    case ast::SyntaxKind::UsingKeyword:
      return "using"_zc;
    case ast::SyntaxKind::WithKeyword:
      return "with"_zc;
    case ast::SyntaxKind::WhenKeyword:
      return "when"_zc;
    case ast::SyntaxKind::SwitchKeyword:
      return "switch"_zc;
    case ast::SyntaxKind::CaseKeyword:
      return "case"_zc;
    case ast::SyntaxKind::MatchKeyword:
      return "match"_zc;
    case ast::SyntaxKind::DoKeyword:
      return "do"_zc;
    case ast::SyntaxKind::DebuggerKeyword:
      return "debugger"_zc;
    case ast::SyntaxKind::InitKeyword:
      return "init"_zc;
    case ast::SyntaxKind::DeinitKeyword:
      return "deinit"_zc;
    case ast::SyntaxKind::RaisesKeyword:
      return "raises"_zc;

    // Type keywords
    case ast::SyntaxKind::BoolKeyword:
      return "bool"_zc;
    case ast::SyntaxKind::I8Keyword:
      return "i8"_zc;
    case ast::SyntaxKind::I32Keyword:
      return "i32"_zc;
    case ast::SyntaxKind::I64Keyword:
      return "i64"_zc;
    case ast::SyntaxKind::U8Keyword:
      return "u8"_zc;
    case ast::SyntaxKind::U16Keyword:
      return "u16"_zc;
    case ast::SyntaxKind::U32Keyword:
      return "u32"_zc;
    case ast::SyntaxKind::U64Keyword:
      return "u64"_zc;
    case ast::SyntaxKind::F32Keyword:
      return "f32"_zc;
    case ast::SyntaxKind::F64Keyword:
      return "f64"_zc;
    case ast::SyntaxKind::StrKeyword:
      return "str"_zc;
    case ast::SyntaxKind::UnitKeyword:
      return "unit"_zc;

    // Common operators
    case ast::SyntaxKind::Plus:
      return "+"_zc;
    case ast::SyntaxKind::Minus:
      return "-"_zc;
    case ast::SyntaxKind::Asterisk:
      return "*"_zc;
    case ast::SyntaxKind::AsteriskAsterisk:
      return "**"_zc;
    case ast::SyntaxKind::Slash:
      return "/"_zc;
    case ast::SyntaxKind::Percent:
      return "%"_zc;
    case ast::SyntaxKind::Equals:
      return "="_zc;
    case ast::SyntaxKind::EqualsEquals:
      return "=="_zc;
    case ast::SyntaxKind::EqualsEqualsEquals:
      return "==="_zc;
    case ast::SyntaxKind::ExclamationEquals:
      return "!="_zc;
    case ast::SyntaxKind::ExclamationEqualsEquals:
      return "!=="_zc;
    case ast::SyntaxKind::LessThan:
      return "<"_zc;
    case ast::SyntaxKind::GreaterThan:
      return ">"_zc;
    case ast::SyntaxKind::LessThanEquals:
      return "<="_zc;
    case ast::SyntaxKind::GreaterThanEquals:
      return ">="_zc;
    case ast::SyntaxKind::AmpersandAmpersand:
      return "&&"_zc;
    case ast::SyntaxKind::BarBar:
      return "||"_zc;
    case ast::SyntaxKind::Exclamation:
      return "!"_zc;
    case ast::SyntaxKind::Question:
      return "?"_zc;
    case ast::SyntaxKind::Colon:
      return ":"_zc;
    case ast::SyntaxKind::Semicolon:
      return ";"_zc;
    case ast::SyntaxKind::Comma:
      return ","_zc;
    case ast::SyntaxKind::Period:
      return "."_zc;
    case ast::SyntaxKind::Arrow:
      return "->"_zc;
    case ast::SyntaxKind::EqualsGreaterThan:
      return "=>"_zc;
    case ast::SyntaxKind::PlusPlus:
      return "++"_zc;
    case ast::SyntaxKind::MinusMinus:
      return "--"_zc;
    case ast::SyntaxKind::PlusEquals:
      return "+="_zc;
    case ast::SyntaxKind::MinusEquals:
      return "-="_zc;
    case ast::SyntaxKind::AsteriskEquals:
      return "*="_zc;
    case ast::SyntaxKind::AsteriskAsteriskEquals:
      return "**="_zc;
    case ast::SyntaxKind::SlashEquals:
      return "/="_zc;
    case ast::SyntaxKind::PercentEquals:
      return "%="_zc;
    case ast::SyntaxKind::Ampersand:
      return "&"_zc;
    case ast::SyntaxKind::Bar:
      return "|"_zc;
    case ast::SyntaxKind::Caret:
      return "^"_zc;
    case ast::SyntaxKind::Tilde:
      return "~"_zc;
    case ast::SyntaxKind::LessThanLessThan:
      return "<<"_zc;
    case ast::SyntaxKind::LessThanSlash:
      return "</"_zc;
    case ast::SyntaxKind::GreaterThanGreaterThan:
      return ">>"_zc;
    case ast::SyntaxKind::GreaterThanGreaterThanGreaterThan:
      return ">"
             ">"
             ">"_zc;
    case ast::SyntaxKind::AmpersandEquals:
      return "&="_zc;
    case ast::SyntaxKind::BarEquals:
      return "|="_zc;
    case ast::SyntaxKind::CaretEquals:
      return "^="_zc;
    case ast::SyntaxKind::LessThanLessThanEquals:
      return "<<="_zc;
    case ast::SyntaxKind::GreaterThanGreaterThanEquals:
      return ">>="_zc;
    case ast::SyntaxKind::GreaterThanGreaterThanGreaterThanEquals:
      return ">>>="_zc;
    case ast::SyntaxKind::BarBarEquals:
      return "||="_zc;
    case ast::SyntaxKind::AmpersandAmpersandEquals:
      return "&&="_zc;
    case ast::SyntaxKind::QuestionQuestion:
      return "??"_zc;
    case ast::SyntaxKind::QuestionQuestionEquals:
      return "\?\?="_zc;
    case ast::SyntaxKind::QuestionDot:
      return "?."_zc;
    case ast::SyntaxKind::DotDotDot:
      return "..."_zc;
    case ast::SyntaxKind::ErrorPropagate:
      return "?!"_zc;
    case ast::SyntaxKind::ErrorUnwrap:
      return "!!"_zc;
    case ast::SyntaxKind::ErrorDefault:
      return "?:"_zc;

    // Punctuation
    case ast::SyntaxKind::LeftParen:
      return "("_zc;
    case ast::SyntaxKind::RightParen:
      return ")"_zc;
    case ast::SyntaxKind::LeftBrace:
      return "{"_zc;
    case ast::SyntaxKind::RightBrace:
      return "}"_zc;
    case ast::SyntaxKind::LeftBracket:
      return "["_zc;
    case ast::SyntaxKind::RightBracket:
      return "]"_zc;
    case ast::SyntaxKind::At:
      return "@"_zc;
    case ast::SyntaxKind::Hash:
      return "#"_zc;
    case ast::SyntaxKind::Backtick:
      return "`"_zc;

    default:
      return ""_zc;
  }
}
}  // namespace

zc::Maybe<zc::String> Token::getStaticTextForTokenKind(ast::SyntaxKind kind) {
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
