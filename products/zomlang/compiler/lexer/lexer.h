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

#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/lexer/token.h"

namespace zomlang {
namespace compiler {

namespace source {
class SourceManager;
class SourceLoc;
class CharSourceRange;
}  // namespace source

namespace diagnostics {
class DiagnosticEngine;
class InFlightDiagnostic;
}  // namespace diagnostics

struct LangOptions;

namespace lexer {

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
  Lexer(const LangOptions& options, const source::SourceManager& sourceMgr,
        diagnostics::DiagnosticEngine& diags, uint64_t bufferId);
  ~Lexer();

  ZC_DISALLOW_COPY_AND_MOVE(Lexer);

  /// For a source location in the current buffer, returns the corresponding
  /// pointer.
  ZC_NODISCARD const char* getBufferPtrForSourceLoc(source::SourceLoc Loc) const;

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
  static unsigned lexUnicodeEscape(const char*& curPtr, diagnostics::DiagnosticEngine& diags);

  /// Regular expression support
  bool tryLexRegexLiteral(const char* tokStart);

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
