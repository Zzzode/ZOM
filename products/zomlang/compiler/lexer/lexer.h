// Copyright (c) 2024 Zode.Z. All rights reserved
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
#include "zomlang/compiler/diagnostics/in-flight-diagnostic.h"
#include "zomlang/compiler/lexer/token.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang {
namespace compiler {

enum class LexerMode {
  kNormal,
  kStringInterpolation,
  kRegexLiteral,
  // more...
};

enum class CommentRetentionMode {
  kNone,               // Leave no comments
  kAttachToNextToken,  // Append a comment to the next tag
  kReturnAsTokens      // Return comments as separate tags
};

struct LexerState {
  const char* ptr;
  LexerMode mode;
  // more...

  LexerState(const char* p, LexerMode m) : ptr(p), mode(m) {}
};

class Lexer {
public:
  // Constructor
  Lexer(const LangOptions& options, const SourceManager& sourceMgr, DiagnosticEngine& diags)
      : langOpts(options), sourceMgr(sourceMgr), diags(diags) {}

  // Main lexical analysis function
  void lex(Token& result);

  // Preview the next token
  const Token& peekNextToken() const;

  // State management
  LexerState getStateForBeginningOfToken(const Token& tok) const;
  void restoreState(LexerState s, bool enableDiagnostics = false);

  // Mode switching
  void enterMode(LexerMode mode);
  void exitMode(LexerMode mode);

  // Unicode support
  static unsigned lexUnicodeEscape(const char*& curPtr, DiagnosticEngine* diags);

  // Regular expression support
  bool tryLexRegexLiteral(const char* tokStart);

  // String interpolation support
  void lexStringLiteral(unsigned customDelimiterLen = 0);

  // Code completion support
  bool isCodeCompletion() const;

  // Error handling and diagnostics
  InFlightDiagnostic diagnose(const char* loc, Diagnostic diag);

  // Comment handling
  void setCommentRetentionMode(CommentRetentionMode mode);

  // Source location and range
  SourceLoc getLocForStartOfToken(SourceLoc loc) const;
  CharSourceRange getCharSourceRangeFromSourceRange(const SourceRange& sr) const;

private:
  // Internal state
  const char* bufferStart;
  const char* bufferEnd;
  const char* curPtr;

  Token nextToken;
  LexerMode currentMode;
  CommentRetentionMode commentMode;

  const LangOptions& langOpts;
  const SourceManager& sourceMgr;
  DiagnosticEngine& diags;

  // Token cache
  zc::Array<TokenDesc> tokenCache;

  // Internal methods
  void formToken(tok kind, const char* tokStart);
  void lexImpl();
  void scanToken();
  void handleNewline();
  void skipTrivia();
  void lexIdentifier();
  void lexNumber();
  void lexStringLiteralImpl();
  void lexEscapedIdentifier();
  void lexOperator();

  // Unicode handling
  uint32_t lexUnicodeScalarValue();

  // Comment handling
  void lexComment();

  // Preprocessor directive handling
  void lexPreprocessorDirective();

  // Multibyte character handling
  bool tryLexMultibyteCharacter();

  // Error recovery
  void recoverFromLexingError();

  // Buffer management
  void refillBuffer();

  // State checks
  bool isAtStartOfLine() const;
  bool isAtEndOfFile() const;

  // Helper functions
  bool isIdentifierStart(char c) const;
  bool isIdentifierContinuation(char c) const;
  bool isOperatorStart(char c) const;
};

}  // namespace compiler
}  // namespace zomlang
