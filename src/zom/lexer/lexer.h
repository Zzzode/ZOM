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

#include "src/zom/basic/lang_options.h"
#include "src/zom/diagnostics/diagnostic_engine.h"
#include "src/zom/diagnostics/in_flight_diagnostic.h"
#include "src/zom/lexer/token.h"
#include "src/zom/source/manager.h"

namespace zom {
namespace lexer {

enum class LexerMode {
  kNormal,
  kStringInterpolation,
  kRegexLiteral,
  // more...
};

enum class CommentRetentionMode {
  kNone,               // 不保留任何注释
  kAttachToNextToken,  // 将注释附加到下一个标记
  kReturnAsTokens      // 将注释作为单独的标记返回
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
  Lexer(const basic::LangOptions& options,
        const source::SourceManager& source_mgr,
        diagnostics::DiagnosticEngine& diags)
      : lang_opts_(options), source_mgr_(source_mgr), diags_(diags) {}

  // Main lexical analysis function
  void Lex(Token& result);

  // Preview the next token
  const Token& PeekNextToken() const;

  // State management
  LexerState GetStateForBeginningOfToken(const Token& tok) const;
  void RestoreState(LexerState s, bool enable_diagnostics = false);

  // Mode switching
  void EnterMode(LexerMode mode);
  void ExitMode(LexerMode mode);

  // Unicode support
  static unsigned LexUnicodeEscape(const char*& cur_ptr,
                                   diagnostics::DiagnosticEngine* diags);

  // Regular expression support
  bool TryLexRegexLiteral(const char* tok_start);

  // String interpolation support
  void LexStringLiteral(unsigned custom_delimiter_len = 0);

  // Code completion support
  bool IsCodeCompletion() const;

  // Error handling and diagnostics
  diagnostics::InFlightDiagnostic Diagnose(const char* loc,
                                           diagnostics::Diagnostic diag);

  // Comment handling
  void SetCommentRetentionMode(CommentRetentionMode mode);

  // Source location and range
  source::SourceLoc GetLocForStartOfToken(source::SourceLoc loc) const;
  source::CharSourceRange GetCharSourceRangeFromSourceRange(
      const source::SourceRange& sr) const;

 private:
  // Internal state
  const char* buffer_start_;
  const char* buffer_end_;
  const char* cur_ptr_;

  Token next_token_;
  LexerMode current_mode_;
  CommentRetentionMode comment_mode_;

  const basic::LangOptions& lang_opts_;
  const source::SourceManager& source_mgr_;
  diagnostics::DiagnosticEngine& diags_;

  // Token cache
  zc::Array<TokenDesc> token_cache_;

  // Internal methods
  void FormToken(tok kind, const char* tok_start);
  void LexImpl();
  void ScanToken();
  void HandleNewline();
  void SkipTrivia();
  void LexIdentifier();
  void LexNumber();
  void LexStringLiteralImpl();
  void LexEscapedIdentifier();
  void LexOperator();

  // Unicode handling
  uint32_t LexUnicodeScalarValue();

  // Comment handling
  void LexComment();

  // Preprocessor directive handling
  void LexPreprocessorDirective();

  // Multibyte character handling
  bool TryLexMultibyteCharacter();

  // Error recovery
  void RecoverFromLexingError();

  // Buffer management
  void RefillBuffer();

  // State checks
  bool IsAtStartOfLine() const;
  bool IsAtEndOfFile() const;

  // Helper functions
  bool IsIdentifierStart(char c) const;
  bool IsIdentifierContinuation(char c) const;
  bool IsOperatorStart(char c) const;
};

}  // namespace lexer
}  // namespace zom