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

#include <cstdint>

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

enum class LexerMode : uint8_t { kNormal, kStringInterpolation, kRegexLiteral };

enum class CommentRetentionMode : uint8_t {
  kNone,               /// Leave no comments
  kAttachToNextToken,  /// Append a comment to the next tag
  kReturnAsTokens      /// Return comments as separate tags
};

struct LexerState {
  source::SourceLoc Loc;
  LexerMode mode;

  explicit LexerState(const source::SourceLoc Loc, const LexerMode m) : Loc(Loc), mode(m) {}
};

class Lexer {
public:
  Lexer(const source::SourceManager& sourceMgr, diagnostics::DiagnosticEngine& diagnosticEngine,
        const basic::LangOptions& options, const source::BufferId& bufferId);
  ~Lexer();

  ZC_DISALLOW_COPY_AND_MOVE(Lexer);

  /// \brief For a source location in the current buffer, returns the corresponding pointer.
  /// \param Loc The source location.
  /// \return The corresponding pointer.
  ZC_NODISCARD const zc::byte* getBufferPtrForSourceLoc(source::SourceLoc Loc) const;

  /// Main lexical analysis function
  void lex(Token& result);

  /// Preview the next token
  const Token& peekNextToken() const;

  /// State management
  LexerState getStateForBeginningOfToken(const Token& tok) const;
  void restoreState(LexerState s, bool enableDiagnostics = false);

  /// Mode switching
  void enterMode(LexerMode mode);
  void exitMode(LexerMode mode);

  /// Unicode support
  static unsigned lexUnicodeEscape(const zc::byte*& curPtr, diagnostics::DiagnosticEngine& diags);

  /// Regular expression support
  bool tryLexRegexLiteral(const zc::byte* tokStart);

  /// String interpolation support
  void lexStringLiteral(unsigned customDelimiterLen = 0);

  /// Code completion support
  bool isCodeCompletion() const;

  /// Comment handling
  void setCommentRetentionMode(CommentRetentionMode mode);

  /// Source location and range
  source::SourceLoc getLocForStartOfToken(source::SourceLoc loc) const;
  source::CharSourceRange getCharSourceRangeFromSourceRange(const source::SourceRange& sr) const;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace lexer
}  // namespace compiler
}  // namespace zomlang
