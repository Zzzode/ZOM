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

#include "zomlang/compiler/source/location.h"

namespace zomlang {
namespace compiler {

namespace source {
class SourceLoc;
}  // namespace source

namespace lexer {

enum class TokenKind {
  kUnknown,
  kIdentifier,
  kKeyword,
  kInteger,
  kFloat,
  kString,
  kOperator,
  kPunctuation,
  kComment,
  kEOF,
  // Add more token types as needed...
};

class Token {
public:
  Token() = default;
  explicit Token(const TokenKind k, zc::StringPtr t, const source::SourceLoc l)
      : kind(k), text(t), loc(l) {}
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