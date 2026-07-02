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

#include <cstddef>

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

class Lexer {
public:
  Lexer(const source::SourceManager& sourceMgr, diagnostics::DiagnosticEngine& diagnosticEngine,
        const basic::LangOptions& options, basic::StringPool& stringPool,
        const source::BufferId& bufferId);
  ~Lexer();

  ZC_DISALLOW_COPY_AND_MOVE(Lexer);

  // =======================================================================================
  // Lexing Utilities

  /// \brief Lex one token into the output token.
  /// \param outToken The token to output.
  void lex(Token& outToken);

  /// \brief Start position of whitespace before current token
  /// \return The source location of the full start.
  ZC_NODISCARD const source::SourceLoc getFullStartLoc() const;

  /// \brief Start position of text of current token
  /// \return The source location of the token start.
  ZC_NODISCARD const source::SourceLoc getTokenStartLoc() const;

  /// \brief Get the list of comment directives found so far.
  /// \return The list of comment directives.
  ZC_NODISCARD const zc::Vector<CommentDirective>& getCommentDirectives() const;

  /// \brief
  /// \return
  ZC_NODISCARD bool hasPrecedingLineBreak() const;

private:
  struct Impl;
  zc::Own<Impl> impl;
};

}  // namespace lexer
}  // namespace compiler
}  // namespace zomlang
