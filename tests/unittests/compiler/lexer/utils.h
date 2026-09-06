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

#include "compiler/basic/string-pool.h"
#include "compiler/basic/zomlang-opts.h"
#include "compiler/diagnostics/fact/source-diagnostic-sink.h"
#include "compiler/lexer/lexer.h"
#include "compiler/source/manager.h"
#include "zc/core/common.h"
#include "zc/core/memory.h"

namespace zomlang {
namespace compiler {
namespace lexer {

struct CapturedLexerDiagnostic final {
  diagnostics::DiagID code;
  zc::Vector<zc::String> arguments;
};

class CapturedLexerDiagnostics final : public diagnostics::SourceDiagnosticSink {
public:
  void addHighlight(diagnostics::SourceDiagnosticDraftHandle,
                    const source::CharSourceRange&) override {}

  ZC_NODISCARD bool hasErrors() const {
    for (const auto& diagnostic : values) {
      if (diagnostics::getDiagnosticInfo(diagnostic.code).severity >=
          diagnostics::DiagSeverity::kError) {
        return true;
      }
    }
    return false;
  }

  ZC_NODISCARD zc::ArrayPtr<const CapturedLexerDiagnostic> diagnostics() const {
    return values.asPtr();
  }

private:
  diagnostics::SourceDiagnosticDraftHandle append(diagnostics::DiagID code, source::SourceLoc,
                                                  zc::Vector<zc::String>&& arguments) override {
    values.add(CapturedLexerDiagnostic{code, zc::mv(arguments)});
    return makeHandle(values.size());
  }

  void appendNote(diagnostics::SourceDiagnosticDraftHandle, diagnostics::DiagID, source::SourceLoc,
                  zc::Vector<zc::String>&&) override {}

  zc::Vector<CapturedLexerDiagnostic> values;
};

struct TokenizeResult {
  zc::Own<basic::StringPool> stringPool;
  zc::Vector<Token> tokens;

  inline size_t size() const { return tokens.size(); }

  inline Token& operator[](size_t i) { return tokens[i]; }
  inline const Token& operator[](size_t i) const { return tokens[i]; }

  inline auto begin() { return tokens.begin(); }
  inline auto end() { return tokens.end(); }
  inline auto begin() const { return tokens.begin(); }
  inline auto end() const { return tokens.end(); }
};

inline source::SourceManager& getSourceManager() {
  static zc::Own<source::SourceManager> sourceManager = zc::heap<source::SourceManager>();
  return *sourceManager;
}

inline zc::Vector<Token> tokenizeWithStringPool(
    zc::StringPtr source, basic::StringPool& stringPool,
    zc::Maybe<CapturedLexerDiagnostics&> diagnostics = zc::none) {
  auto& sourceManager = getSourceManager();
  auto langOpts = basic::LangOptions();

  auto bufferId = sourceManager.addMemBufferCopy(source.asBytes(), "test.zom");

  zc::Own<CapturedLexerDiagnostics> ownedDiagnostics;
  CapturedLexerDiagnostics* diagnosticSink = nullptr;

  ZC_IF_SOME(value, diagnostics) {
    diagnosticSink = &value;
  } else {
    ownedDiagnostics = zc::heap<CapturedLexerDiagnostics>();
    diagnosticSink = ownedDiagnostics.get();
  }

  Lexer lexer(sourceManager, *diagnosticSink, langOpts, stringPool, bufferId);

  zc::Vector<Token> tokens;
  Token token;
  do {
    lexer.lex(token);
    tokens.add(token);
  } while (token.getKind() != ast::SyntaxKind::EndOfFile);

  return tokens;
}

inline TokenizeResult tokenize(zc::StringPtr source) {
  TokenizeResult result;
  result.stringPool = zc::heap<basic::StringPool>();
  result.tokens = tokenizeWithStringPool(source, *result.stringPool);
  return result;
}

inline TokenizeResult tokenize(zc::StringPtr source, CapturedLexerDiagnostics& diagnostics) {
  TokenizeResult result;
  result.stringPool = zc::heap<basic::StringPool>();
  result.tokens = tokenizeWithStringPool(source, *result.stringPool, diagnostics);
  return result;
}

inline TokenizeResult tokenize(zc::StringPtr source, CapturedLexerDiagnostics* diagnostics) {
  if (diagnostics == nullptr) { return tokenize(source); }
  return tokenize(source, *diagnostics);
}

inline zc::Vector<CommentDirective> tokenizeAndGetDirectives(zc::StringPtr source) {
  auto& sourceManager = getSourceManager();
  auto langOpts = basic::LangOptions();

  auto bufferId = sourceManager.addMemBufferCopy(source.asBytes(), "test_directives.zom");

  CapturedLexerDiagnostics diagnostics;
  basic::StringPool stringPool;
  Lexer lexer(sourceManager, diagnostics, langOpts, stringPool, bufferId);

  Token token;
  do { lexer.lex(token); } while (token.getKind() != ast::SyntaxKind::EndOfFile);

  zc::Vector<CommentDirective> directives;
  for (const auto& dir : lexer.getCommentDirectives()) { directives.add(dir); }
  return directives;
}

}  // namespace lexer
}  // namespace compiler
}  // namespace zomlang
