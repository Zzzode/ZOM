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

#include "zomlang/compiler/lexer/utils.h"

#include "zomlang/compiler/lexer/unicode-data.h"

namespace zomlang {
namespace compiler {
namespace lexer {

uint32_t _::ParseOctalValue::operator()(const zc::ArrayPtr<const zc::byte>& octalDigits) const {
  uint32_t value = 0;
  for (zc::byte digit : octalDigits) { value = (value << 3) | (digit - '0'); }
  return value;
}

zc::String _::FormatHexValue::operator()(uint32_t value) const {
  zc::String hexValue = zc::str(zc::hex(value));
  if (hexValue.size() < 2) { hexValue = zc::str("0", hexValue); }
  return hexValue;
}

uint32_t parseOctalValue(const zc::ArrayPtr<const zc::byte>& octalDigits) {
  return _::ParseOctalValue()(octalDigits);
}

zc::String formatHexValue(uint32_t value) { return _::FormatHexValue()(value); }

zc::String parseOctalAndFormatHex(const zc::ArrayPtr<const zc::byte>& octalDigits) {
  uint32_t value = parseOctalValue(octalDigits);
  return formatHexValue(value);
}

uint32_t parseHexValue(zc::StringPtr hexDigits) {
  uint32_t value = 0;
  for (char c : hexDigits) {
    uint32_t digit;
    if (c >= '0' && c <= '9') {
      digit = c - '0';
    } else if (c >= 'a' && c <= 'f') {
      digit = c - 'a' + 10;
    } else if (c >= 'A' && c <= 'F') {
      digit = c - 'A' + 10;
    } else {
      digit = 0;
    }
    value = (value << 4) | digit;
  }
  return value;
}

zc::String encodeUtf8(uint32_t code) {
  char buffer[4];
  size_t len = 0;
  if (code <= 0x7F) {
    buffer[len++] = static_cast<char>(code);
  } else if (code <= 0x7FF) {
    buffer[len++] = static_cast<char>((0xC0 | (code >> 6)));
    buffer[len++] = static_cast<char>((0x80 | (code & 0x3F)));
  } else if (code <= 0xFFFF) {
    buffer[len++] = static_cast<char>((0xE0 | (code >> 12)));
    buffer[len++] = static_cast<char>((0x80 | ((code >> 6) & 0x3F)));
    buffer[len++] = static_cast<char>((0x80 | (code & 0x3F)));
  } else if (code <= 0x10FFFF) {
    buffer[len++] = static_cast<char>((0xF0 | (code >> 18)));
    buffer[len++] = static_cast<char>((0x80 | ((code >> 12) & 0x3F)));
    buffer[len++] = static_cast<char>((0x80 | ((code >> 6) & 0x3F)));
    buffer[len++] = static_cast<char>((0x80 | (code & 0x3F)));
  }
  return zc::heapString(zc::ArrayPtr<const char>(buffer, len));
}

std::pair<uint32_t, uint32_t> tryDecodeUtf8CodePoint(const zc::byte* ptr, const zc::byte* end) {
  if (ptr >= end) { return {kInvalidCodePoint, 0}; }

  zc::byte c = *ptr;
  uint32_t codePoint = 0;

  if (c < 0x80) { return {c, 1}; }

  if ((c & 0xe0) == 0xc0) {
    if (ptr + 1 >= end) { return {kInvalidCodePoint, 1}; }
    zc::byte c2 = ptr[1];
    if ((c2 & 0xc0) != 0x80) { return {kInvalidCodePoint, 1}; }

    codePoint = ((c & 0x1f) << 6) | (c2 & 0x3f);
    if (codePoint < 0x80) { return {kInvalidCodePoint, 1}; }

    return {codePoint, 2};
  }

  if ((c & 0xf0) == 0xe0) {
    if (ptr + 2 >= end) { return {kInvalidCodePoint, 1}; }
    zc::byte c2 = ptr[1];
    zc::byte c3 = ptr[2];
    if ((c2 & 0xc0) != 0x80 || (c3 & 0xc0) != 0x80) { return {kInvalidCodePoint, 1}; }

    codePoint = ((c & 0x0f) << 12) | ((c2 & 0x3f) << 6) | (c3 & 0x3f);
    if (codePoint < 0x800) { return {kInvalidCodePoint, 1}; }
    if (codePoint >= 0xd800 && codePoint <= 0xdfff) { return {kInvalidCodePoint, 1}; }

    return {codePoint, 3};
  }

  if ((c & 0xf8) == 0xf0) {
    if (ptr + 3 >= end) { return {kInvalidCodePoint, 1}; }
    zc::byte c2 = ptr[1];
    zc::byte c3 = ptr[2];
    zc::byte c4 = ptr[3];
    if ((c2 & 0xc0) != 0x80 || (c3 & 0xc0) != 0x80 || (c4 & 0xc0) != 0x80) {
      return {kInvalidCodePoint, 1};
    }

    codePoint = ((c & 0x07) << 18) | ((c2 & 0x3f) << 12) | ((c3 & 0x3f) << 6) | (c4 & 0x3f);
    if (codePoint < 0x10000) { return {kInvalidCodePoint, 1}; }
    if (codePoint > 0x10ffff) { return {kInvalidCodePoint, 1}; }

    return {codePoint, 4};
  }

  return {kInvalidCodePoint, 1};
}

std::pair<uint32_t, uint32_t> charWithSize(const zc::byte* ptr, const zc::byte* end) {
  auto result = tryDecodeUtf8CodePoint(ptr, end);
  return result;
}

bool isValidIdentifier(zc::StringPtr s) {
  if (s.size() == 0) { return false; }

  const zc::byte* ptr = s.asBytes().begin();
  const zc::byte* end = s.asBytes().end();

  bool isFirst = true;
  while (ptr < end) {
    auto result = charWithSize(ptr, end);
    uint32_t codePoint = result.first;
    uint32_t size = result.second;

    if (size == 0) { return false; }

    if (isFirst) {
      if (!isIdentifierStart(codePoint)) { return false; }
      isFirst = false;
    } else {
      if (!isIdentifierPart(codePoint)) { return false; }
    }
    ptr += size;
  }
  return true;
}

ast::SyntaxKind getKeywordKind(zc::ArrayPtr<const zc::byte> text) {
  if (text == "abstract"_zcb) return ast::SyntaxKind::AbstractKeyword;
  if (text == "accessor"_zcb) return ast::SyntaxKind::AccessorKeyword;
  if (text == "any"_zcb) return ast::SyntaxKind::AnyKeyword;
  if (text == "as"_zcb) return ast::SyntaxKind::AsKeyword;
  if (text == "asserts"_zcb) return ast::SyntaxKind::AssertsKeyword;
  if (text == "assert"_zcb) return ast::SyntaxKind::AssertKeyword;
  if (text == "async"_zcb) return ast::SyntaxKind::AsyncKeyword;
  if (text == "await"_zcb) return ast::SyntaxKind::AwaitKeyword;
  if (text == "bigint"_zcb) return ast::SyntaxKind::BigIntKeyword;
  if (text == "bool"_zcb) return ast::SyntaxKind::BoolKeyword;
  if (text == "break"_zcb) return ast::SyntaxKind::BreakKeyword;
  if (text == "case"_zcb) return ast::SyntaxKind::CaseKeyword;
  if (text == "catch"_zcb) return ast::SyntaxKind::CatchKeyword;
  if (text == "class"_zcb) return ast::SyntaxKind::ClassKeyword;
  if (text == "continue"_zcb) return ast::SyntaxKind::ContinueKeyword;
  if (text == "const"_zcb) return ast::SyntaxKind::ConstKeyword;
  if (text == "constructor"_zcb) return ast::SyntaxKind::ConstructorKeyword;
  if (text == "debugger"_zcb) return ast::SyntaxKind::DebuggerKeyword;
  if (text == "declare"_zcb) return ast::SyntaxKind::DeclareKeyword;
  if (text == "default"_zcb) return ast::SyntaxKind::DefaultKeyword;
  if (text == "delete"_zcb) return ast::SyntaxKind::DeleteKeyword;
  if (text == "do"_zcb) return ast::SyntaxKind::DoKeyword;
  if (text == "extends"_zcb) return ast::SyntaxKind::ExtendsKeyword;
  if (text == "export"_zcb) return ast::SyntaxKind::ExportKeyword;
  if (text == "false"_zcb) return ast::SyntaxKind::FalseKeyword;
  if (text == "finally"_zcb) return ast::SyntaxKind::FinallyKeyword;
  if (text == "from"_zcb) return ast::SyntaxKind::FromKeyword;
  if (text == "fun"_zcb) return ast::SyntaxKind::FunKeyword;
  if (text == "get"_zcb) return ast::SyntaxKind::GetKeyword;
  if (text == "global"_zcb) return ast::SyntaxKind::GlobalKeyword;
  if (text == "if"_zcb) return ast::SyntaxKind::IfKeyword;
  if (text == "immediate"_zcb) return ast::SyntaxKind::ImmediateKeyword;
  if (text == "implements"_zcb) return ast::SyntaxKind::ImplementsKeyword;
  if (text == "import"_zcb) return ast::SyntaxKind::ImportKeyword;
  if (text == "in"_zcb) return ast::SyntaxKind::InKeyword;
  if (text == "infer"_zcb) return ast::SyntaxKind::InferKeyword;
  if (text == "instanceof"_zcb) return ast::SyntaxKind::InstanceOfKeyword;
  if (text == "interface"_zcb) return ast::SyntaxKind::InterfaceKeyword;
  if (text == "intrinsic"_zcb) return ast::SyntaxKind::IntrinsicKeyword;
  if (text == "is"_zcb) return ast::SyntaxKind::IsKeyword;
  if (text == "keyof"_zcb) return ast::SyntaxKind::KeyOfKeyword;
  if (text == "let"_zcb) return ast::SyntaxKind::LetKeyword;
  if (text == "match"_zcb) return ast::SyntaxKind::MatchKeyword;
  if (text == "module"_zcb) return ast::SyntaxKind::ModuleKeyword;
  if (text == "mutating"_zcb) return ast::SyntaxKind::MutatingKeyword;
  if (text == "namespace"_zcb) return ast::SyntaxKind::NamespaceKeyword;
  if (text == "never"_zcb) return ast::SyntaxKind::NeverKeyword;
  if (text == "new"_zcb) return ast::SyntaxKind::NewKeyword;
  if (text == "null"_zcb) return ast::SyntaxKind::NullKeyword;
  if (text == "object"_zcb) return ast::SyntaxKind::ObjectKeyword;
  if (text == "of"_zcb) return ast::SyntaxKind::OfKeyword;
  if (text == "optional"_zcb) return ast::SyntaxKind::OptionalKeyword;
  if (text == "out"_zcb) return ast::SyntaxKind::OutKeyword;
  if (text == "override"_zcb) return ast::SyntaxKind::OverrideKeyword;
  if (text == "package"_zcb) return ast::SyntaxKind::PackageKeyword;
  if (text == "private"_zcb) return ast::SyntaxKind::PrivateKeyword;
  if (text == "protected"_zcb) return ast::SyntaxKind::ProtectedKeyword;
  if (text == "public"_zcb) return ast::SyntaxKind::PublicKeyword;
  if (text == "readonly"_zcb) return ast::SyntaxKind::ReadonlyKeyword;
  if (text == "require"_zcb) return ast::SyntaxKind::RequireKeyword;
  if (text == "return"_zcb) return ast::SyntaxKind::ReturnKeyword;
  if (text == "satisfies"_zcb) return ast::SyntaxKind::SatisfiesKeyword;
  if (text == "set"_zcb) return ast::SyntaxKind::SetKeyword;
  if (text == "static"_zcb) return ast::SyntaxKind::StaticKeyword;
  if (text == "super"_zcb) return ast::SyntaxKind::SuperKeyword;
  if (text == "switch"_zcb) return ast::SyntaxKind::SwitchKeyword;
  if (text == "symbol"_zcb) return ast::SyntaxKind::SymbolKeyword;
  if (text == "this"_zcb) return ast::SyntaxKind::ThisKeyword;
  if (text == "throw"_zcb) return ast::SyntaxKind::ThrowKeyword;
  if (text == "true"_zcb) return ast::SyntaxKind::TrueKeyword;
  if (text == "try"_zcb) return ast::SyntaxKind::TryKeyword;
  if (text == "typeof"_zcb) return ast::SyntaxKind::TypeOfKeyword;
  if (text == "undefined"_zcb) return ast::SyntaxKind::UndefinedKeyword;
  if (text == "unique"_zcb) return ast::SyntaxKind::UniqueKeyword;
  if (text == "using"_zcb) return ast::SyntaxKind::UsingKeyword;
  if (text == "when"_zcb) return ast::SyntaxKind::WhenKeyword;
  if (text == "with"_zcb) return ast::SyntaxKind::WithKeyword;
  if (text == "yield"_zcb) return ast::SyntaxKind::YieldKeyword;

  if (text == "bool"_zcb) return ast::SyntaxKind::BoolKeyword;
  if (text == "i8"_zcb) return ast::SyntaxKind::I8Keyword;
  if (text == "i32"_zcb) return ast::SyntaxKind::I32Keyword;
  if (text == "i64"_zcb) return ast::SyntaxKind::I64Keyword;
  if (text == "u8"_zcb) return ast::SyntaxKind::U8Keyword;
  if (text == "u16"_zcb) return ast::SyntaxKind::U16Keyword;
  if (text == "u32"_zcb) return ast::SyntaxKind::U32Keyword;
  if (text == "u64"_zcb) return ast::SyntaxKind::U64Keyword;
  if (text == "f32"_zcb) return ast::SyntaxKind::F32Keyword;
  if (text == "f64"_zcb) return ast::SyntaxKind::F64Keyword;
  if (text == "str"_zcb) return ast::SyntaxKind::StrKeyword;
  if (text == "unit"_zcb) return ast::SyntaxKind::UnitKeyword;
  if (text == "null"_zcb) return ast::SyntaxKind::NullKeyword;
  if (text == "else"_zcb) return ast::SyntaxKind::ElseKeyword;
  if (text == "for"_zcb) return ast::SyntaxKind::ForKeyword;
  if (text == "while"_zcb) return ast::SyntaxKind::WhileKeyword;
  if (text == "struct"_zcb) return ast::SyntaxKind::StructKeyword;
  if (text == "enum"_zcb) return ast::SyntaxKind::EnumKeyword;
  if (text == "error"_zcb) return ast::SyntaxKind::ErrorKeyword;
  if (text == "alias"_zcb) return ast::SyntaxKind::AliasKeyword;
  if (text == "init"_zcb) return ast::SyntaxKind::InitKeyword;
  if (text == "deinit"_zcb) return ast::SyntaxKind::DeinitKeyword;
  if (text == "raises"_zcb) return ast::SyntaxKind::RaisesKeyword;
  if (text == "type"_zcb) return ast::SyntaxKind::TypeKeyword;

  return ast::SyntaxKind::Identifier;
}

// =======================================================================================
// Character Classification Utilities

bool isIdentifierStart(uint32_t c) {
  if (c < 0x80) { return std::isalpha(c) || c == '_' || c == '$'; }
  return isIdStart(c);
}

bool isIdentifierPart(uint32_t c) {
  if (c < 0x80) { return std::isalnum(c) || c == '_' || c == '$'; }
  return isIdPart(c);
}

bool isUnicodeIdentifierStart(const uint32_t codePoint) { return isIdStart(codePoint); }

bool isUnicodeIdentifierPart(const uint32_t codePoint) { return isIdPart(codePoint); }

bool isWordCharacter(uint32_t c) { return isalnum(c) || c == '_'; }

bool isASCIILetter(int32_t ch) { return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'); }

bool isDigit(int32_t ch) { return isdigit(ch); }

bool isOctalDigit(zc::byte c) { return c >= '0' && c <= '7'; }

bool isHexDigit(int32_t ch) { return std::isxdigit(ch); }

bool isWhiteSpaceSingleLine(uint32_t code) {
  switch (code) {
    case ' ':     // space
    case '\t':    // tab
    case '\v':    // vertical tab
    case '\f':    // form feed
    case 0x0085:  // next line
    case 0x00A0:  // no-break space
    case 0x1680:  // ogham space mark
    case 0x2000:  // en quad
    case 0x2001:  // em quad
    case 0x2002:  // en space
    case 0x2003:  // em space
    case 0x2004:  // three-per-em space
    case 0x2005:  // four-per-em space
    case 0x2006:  // six-per-em space
    case 0x2007:  // figure space
    case 0x2008:  // punctuation-em space
    case 0x2009:  // thin space
    case 0x200A:  // hair space
    case 0x200B:  // zero width space
    case 0x202F:  // narrow no-break space
    case 0x205F:  // medium mathematical space
    case 0x3000:  // ideographic space
    case 0xFEFF:  // byte order mark
      return true;
  }
  return false;
}

bool isLineBreak(uint32_t code) {
  return code == '\n' || code == '\r' || code == 0x2028 || code == 0x2029;
}

// =======================================================================================
// Keyword Utilities

bool isKeyword(ast::SyntaxKind tokenKind) {
  return tokenKind >= ast::SyntaxKind::FirstKeyword && tokenKind <= ast::SyntaxKind::LastKeyword;
}

bool isReservedKeyword(ast::SyntaxKind tokenKind) {
  return tokenKind >= ast::SyntaxKind::FirstReservedWord &&
         tokenKind <= ast::SyntaxKind::LastReservedWord;
}

bool isIdentifierOrKeyword(ast::SyntaxKind tokenKind) {
  return tokenKind == ast::SyntaxKind::Identifier || lexer::isKeyword(tokenKind);
}

}  // namespace lexer
}  // namespace compiler
}  // namespace zomlang
