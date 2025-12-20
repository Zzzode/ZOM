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

#include "zc/core/common.h"
#include "zc/core/memory.h"
#include "zomlang/compiler/basic/string-pool.h"
#include "zomlang/compiler/basic/zomlang-opts.h"
#include "zomlang/compiler/diagnostics/diagnostic-engine.h"
#include "zomlang/compiler/lexer/lexer.h"
#include "zomlang/compiler/source/manager.h"

namespace zomlang {
namespace compiler {
namespace lexer {

inline source::SourceManager& getSourceManager() {
  static zc::Own<source::SourceManager> sourceManager = zc::heap<source::SourceManager>();
  return *sourceManager;
}

inline zc::Vector<Token> tokenize(zc::StringPtr source,
                                  zc::Maybe<diagnostics::DiagnosticEngine&> diagEng = zc::none) {
  auto& sourceManager = getSourceManager();
  auto langOpts = basic::LangOptions();
  basic::StringPool stringPool;

  auto bufferId = sourceManager.addMemBufferCopy(source.asBytes(), "test.zom");

  zc::Own<diagnostics::DiagnosticEngine> ownedDiagEng;
  diagnostics::DiagnosticEngine* pDiagEng = nullptr;

  ZC_IF_SOME(DE, diagEng) { pDiagEng = &DE; }
  else {
    ownedDiagEng = zc::heap<diagnostics::DiagnosticEngine>(sourceManager);
    pDiagEng = ownedDiagEng.get();
  }

  Lexer lexer(sourceManager, *pDiagEng, langOpts, stringPool, bufferId);

  zc::Vector<Token> tokens;
  Token token;
  do {
    lexer.lex(token);
    tokens.add(token);
  } while (token.getKind() != ast::SyntaxKind::EndOfFile);

  return tokens;
}

inline zc::Vector<CommentDirective> tokenizeAndGetDirectives(zc::StringPtr source) {
  auto& sourceManager = getSourceManager();
  auto langOpts = basic::LangOptions();

  auto bufferId = sourceManager.addMemBufferCopy(source.asBytes(), "test_directives.zom");

  auto ownedDiagEng = zc::heap<diagnostics::DiagnosticEngine>(sourceManager);
  basic::StringPool stringPool;
  Lexer lexer(sourceManager, *ownedDiagEng, langOpts, stringPool, bufferId);

  Token token;
  do { lexer.lex(token); } while (token.getKind() != ast::SyntaxKind::EndOfFile);

  zc::Vector<CommentDirective> directives;
  for (const auto& dir : lexer.getCommentDirectives()) { directives.add(dir); }
  return directives;
}

}  // namespace lexer
}  // namespace compiler
}  // namespace zomlang
