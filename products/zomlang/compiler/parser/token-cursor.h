// Copyright (c) 2024-2025 Zode.Z. All rights reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <cstddef>

#include "zc/core/common.h"
#include "zc/core/string.h"
#include "zomlang/compiler/ast/kinds.h"
#include "zomlang/compiler/lexer/token.h"

namespace zomlang {
namespace compiler {

namespace diagnostics {
class DiagnosticEngine;
}

namespace parser {

/// \brief Non-owning indexed cursor over a token stream.
class TokenCursor {
public:
  using Mark = size_t;

  TokenCursor() = default;
  explicit TokenCursor(zc::ArrayPtr<const lexer::Token> tokens);

  /// \brief Replace the token stream and reset the current position.
  void reset(zc::ArrayPtr<const lexer::Token> tokens);

  /// \brief Return the number of tokens in the referenced stream.
  ZC_NODISCARD size_t size() const;

  /// \brief Return the current token index.
  ZC_NODISCARD size_t position() const;

  /// \brief Return the token kind at the current position plus offset.
  ZC_NODISCARD ast::SyntaxKind peek(size_t offset = 0) const;

  /// \brief Return the token at the current position plus offset.
  ZC_NODISCARD const lexer::Token& token(size_t offset = 0) const;

  /// \brief Return the token at an absolute token index.
  ZC_NODISCARD const lexer::Token& tokenAt(size_t index) const;

  /// \brief Check whether the current token has the requested kind.
  ZC_NODISCARD bool at(ast::SyntaxKind kind) const;

  /// \brief Consume the current token when it has the requested kind.
  bool eat(ast::SyntaxKind kind);

  /// \brief Advance by one token unless already positioned at EOF.
  void advance();

  /// \brief Move to an absolute token index.
  void moveTo(size_t index);

  /// \brief Consume the expected token or report a parser diagnostic.
  bool expect(ast::SyntaxKind kind, diagnostics::DiagnosticEngine& diagnosticEngine,
              zc::StringPtr expected);

  /// \brief Save the current token position.
  ZC_NODISCARD Mark mark() const;

  /// \brief Restore a previously saved token position.
  void rewind(Mark mark);

  /// \brief Return true when the cursor is positioned at EOF.
  ZC_NODISCARD bool isAtEnd() const;

private:
  zc::ArrayPtr<const lexer::Token> tokens;
  size_t current = 0;

  ZC_NODISCARD size_t eofIndex() const;
  ZC_NODISCARD size_t relativeIndex(size_t offset) const;
};

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
