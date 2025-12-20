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

#include "zc/core/vector.h"
#include "zomlang/compiler/basic/string-pool.h"
#include "zomlang/compiler/lexer/token.h"
#include "zomlang/compiler/source/location.h"

namespace zomlang {
namespace compiler {

namespace source {
class BufferId;
class SourceManager;
}  // namespace source

namespace diagnostics {
class DiagnosticEngine;
class InFlightDiagnostic;
}  // namespace diagnostics

namespace basic {
struct LangOptions;
}

namespace lexer {

// Forward declarations
class Token;

enum class CommentDirectiveKind {
  ExpectError,
  Ignore,
};

struct CommentDirective {
  source::SourceRange range;
  CommentDirectiveKind kind;
};

struct LexerState {
  // Core position pointers
  const zc::byte* curPtr;         // Current position (end position of text of current token)
  const zc::byte* fullStartPtr;   // Start position of whitespace before current token
  const zc::byte* tokenStartPtr;  // Start position of text of current token

  TokenFlags tokenFlags;
  // Token state
  Token token;  // Current token

  explicit LexerState(const zc::byte* curPtr = nullptr, const zc::byte* fullStartPtr = nullptr,
                      const zc::byte* tokenStartPtr = nullptr, const Token& token = Token(),
                      TokenFlags tokenFlags = TokenFlags::None)
      : curPtr(curPtr),
        fullStartPtr(fullStartPtr),
        tokenStartPtr(tokenStartPtr),
        tokenFlags(tokenFlags),
        token(token) {}
};

class Lexer {
public:
  Lexer(const source::SourceManager& sourceMgr, diagnostics::DiagnosticEngine& diagnosticEngine,
        const basic::LangOptions& options, basic::StringPool& stringPool,
        const source::BufferId& bufferId);
  ~Lexer();

  ZC_DISALLOW_COPY_AND_MOVE(Lexer);

  // =======================================================================================
  // Lexing Utilities

  /// \brief Lex and output token by reference (for tests/back-compat)
  /// \param outToken The token to output.
  void lex(Token& outToken);

  /// \brief Restore the lexer state.
  /// \param s The state to restore.
  /// \param enableDiagnostics Whether to enable diagnostics.
  void restoreState(LexerState s, bool enableDiagnostics = false);

  /// \brief Get the current lexer state.
  /// \return The current lexer state.
  ZC_NODISCARD const LexerState getCurrentState() const;

  /// \brief Start position of whitespace before current token
  /// \return The source location of the full start.
  ZC_NODISCARD const source::SourceLoc getFullStartLoc() const;

  /// \brief Get the list of comment directives found so far.
  /// \return The list of comment directives.
  ZC_NODISCARD const zc::Vector<CommentDirective>& getCommentDirectives() const;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace lexer
}  // namespace compiler
}  // namespace zomlang
