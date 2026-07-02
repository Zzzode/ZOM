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
#include "zc/core/vector.h"
#include "zomlang/compiler/ast/kinds.h"
#include "zomlang/compiler/lexer/token.h"

namespace zomlang {
namespace compiler {

namespace diagnostics {
class DiagnosticEngine;
}

namespace basic {
struct LangOptions;
class StringPool;
}  // namespace basic

namespace source {
class BufferId;
class SourceManager;
}  // namespace source

namespace lexer {
class Lexer;
}  // namespace lexer

namespace parser {

/// \brief Lazy token stream used by the hand-written LL(k) parser.
class TokenStream {
public:
  TokenStream();
  TokenStream(const source::SourceManager& sourceMgr,
              diagnostics::DiagnosticEngine& diagnosticEngine, const basic::LangOptions& langOpts,
              basic::StringPool& stringPool, const source::BufferId& bufferId);
  ~TokenStream() noexcept(false);

  ZC_DISALLOW_COPY_AND_MOVE(TokenStream);

  /// \brief Reset to a fixed token stream for focused cursor/context tests.
  void reset(zc::ArrayPtr<const lexer::Token> tokens);

  /// \brief Return the currently buffered token count without forcing EOF.
  ZC_NODISCARD size_t bufferedSize() const;

  /// \brief Return the currently buffered non-EOF token count without forcing EOF.
  ZC_NODISCARD size_t bufferedTokenLimit() const;

  /// \brief Return true after the stream has lexed EOF.
  ZC_NODISCARD bool hasBufferedEof() const;

  /// \brief Return the token at an absolute index, lexing as needed.
  ZC_NODISCARD const lexer::Token& tokenAt(size_t index) const;

  /// \brief Return the kind at an absolute index, lexing as needed.
  ZC_NODISCARD ast::SyntaxKind kindAt(size_t index) const;

  /// \brief Clamp an absolute index to EOF after lexing as needed.
  ZC_NODISCARD size_t clampIndex(size_t index) const;

private:
  mutable zc::Own<lexer::Lexer> lexer;
  mutable zc::Vector<lexer::Token> tokens;
  mutable bool reachedEof = false;

  void ensure(size_t index) const;
  void lexNext() const;
  ZC_NODISCARD size_t eofIndex() const;
};

/// \brief Non-owning indexed cursor over a token stream.
class TokenCursor {
public:
  struct Mark {
    size_t current = 0;
    bool splitMode = false;
    int splitRemaining = 0;
    ast::SyntaxKind splitOriginalKind = ast::SyntaxKind::Unknown;
    lexer::Token splitVirtualToken;
  };

  class ScopedSplitMode {
  public:
    explicit ScopedSplitMode(TokenCursor& cursor);
    ~ScopedSplitMode();

    ScopedSplitMode(const ScopedSplitMode&) = delete;
    ScopedSplitMode& operator=(const ScopedSplitMode&) = delete;

  private:
    TokenCursor& cursor;
    bool previousSplitMode = false;
  };

  TokenCursor() = default;
  explicit TokenCursor(TokenStream& stream);

  /// \brief Replace the token stream and reset the current position.
  void reset(TokenStream& stream);

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

  /// \name Split mode
  /// When active, maximal right-shift tokens (>>, >>>) are exposed as individual
  /// GreaterThan tokens. This is used in type-argument contexts where closing
  /// angle brackets must be matched one-for-one.
  ///@{

  /// \brief Enable right-angle split mode.
  void enableSplitMode();

  /// \brief Enable right-angle split mode for the current scope.
  ZC_NODISCARD ScopedSplitMode scopedSplitMode();

  /// \brief Disable right-angle split mode and clear any in-progress split.
  void disableSplitMode();

  /// \brief Query whether split mode is currently enabled.
  ZC_NODISCARD bool isSplitModeActive() const;

  ///@}

private:
  TokenStream* stream = nullptr;
  size_t current = 0;

  // Split mode state (mutable because primeSplitState() is called from const methods)
  bool splitMode_{false};
  /// Number of virtual > tokens remaining to be consumed from the current
  /// maximal right-shift token. 0 means we are not mid-split.
  mutable int splitRemaining_{0};
  /// The original maximal token kind that is currently being split.
  mutable ast::SyntaxKind splitOriginalKind_{ast::SyntaxKind::Unknown};
  /// Cached virtual > token returned by token() while mid-split.
  mutable lexer::Token splitVirtualToken_;

  ZC_NODISCARD size_t relativeIndex(size_t offset) const;

  /// Initialize split state for the current token if it is a maximal right-shift.
  void primeSplitState() const;

  /// Restore split-mode state after a scoped guard exits.
  void restoreScopedSplitMode(bool wasActive);

  /// Return the number of > characters represented by the given kind.
  static int rightAngleCount(ast::SyntaxKind kind);
};

}  // namespace parser
}  // namespace compiler
}  // namespace zomlang
